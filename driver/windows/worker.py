"""Isolated Windows x64 RSA_API worker; JSONL stdout, cooperative stdin stop.

Uses the documented Tektronix C ABI (programmer reference 077103104) and the
same calling convention/96-byte Spectrum_Settings verified by our reference
recorder. No Tektronix software, firmware or calibration data is redistributed.
"""
from __future__ import annotations
import argparse
from contextlib import ExitStack, contextmanager, nullcontext
import ctypes as C
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import struct
import sys
import threading
import time

MIN_HZ, MAX_HZ = 9000.0, 6.2e9
MIN_RBW, MAX_RBW = 1000.0, 1e6
TRACE_LENGTH = 4001
MAX_SEGMENTS = 512


class Settings(C.Structure):
    _fields_ = [('span', C.c_double), ('rbw', C.c_double), ('enableVBW', C.c_bool),
                ('vbw', C.c_double), ('traceLength', C.c_int), ('window', C.c_int),
                ('verticalUnit', C.c_int), ('actualStartFreq', C.c_double),
                ('actualStopFreq', C.c_double), ('actualFreqStepSize', C.c_double),
                ('actualRBW', C.c_double), ('actualVBW', C.c_double), ('actualNumIQSamples', C.c_int)]


class TraceInfo(C.Structure):
    _fields_ = [('timestamp', C.c_uint64), ('acqDataStatus', C.c_uint16)]


@dataclass(frozen=True)
class Config:
    start: float = 470e6
    stop: float = 694e6
    rbw: float = 10000
    reference: float = -20
    points: int = 1001

    def validate(self):
        if not all(math.isfinite(x) for x in (self.start, self.stop, self.rbw, self.reference)):
            raise ValueError('Scan settings must be finite')
        if not MIN_HZ <= self.start < self.stop <= MAX_HZ:
            raise ValueError('Scan must be within 9 kHz–6.2 GHz')
        if not MIN_RBW <= self.rbw <= MAX_RBW or self.stop-self.start < self.rbw*3:
            raise ValueError('Require RBW 1 kHz–1 MHz and span at least three RBWs')
        if not -50 <= self.reference <= 20:
            raise ValueError('Reference level must be -50…+20 dBm')
        if type(self.points) is not int or not 2 <= self.points <= 2000:
            raise ValueError('Output point count must be 2…2000')
        return self


def plan(config):
    config.validate()
    count = math.ceil((config.stop-config.start)/20e6)
    if count > MAX_SEGMENTS:
        raise ValueError('Too many sequential segments')
    width = (config.stop-config.start)/count
    # Overlap adjacent segments, staying within the physical instrument limits.
    # The API can return an exclusive end bin; uniform peak cells tolerate the
    # small endpoint gap. Actual axes are always checked before publication.
    return [{'start_hz': max(MIN_HZ, config.start+i*width-width*.025),
             'stop_hz': min(MAX_HZ, config.start+(i+1)*width+width*.025)} for i in range(count)]


def emit(record):
    print(json.dumps(record, allow_nan=False, separators=(',', ':')), flush=True)


class Api:
    def __init__(self, dll):
        self.dll = dll
        signatures = {
            'DEVICE_GetAPIVersion': [C.POINTER(C.c_char)],
            'DEVICE_Search': [C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(C.c_char), C.POINTER(C.c_char)],
            'DEVICE_Connect': [C.c_int], 'DEVICE_Disconnect': [],
            'DEVICE_GetSerialNumber': [C.POINTER(C.c_char)], 'DEVICE_GetNomenclature': [C.POINTER(C.c_char)],
            'DEVICE_GetHWVersion': [C.POINTER(C.c_char)], 'DEVICE_GetFWVersion': [C.POINTER(C.c_char)],
            'DEVICE_GetFPGAVersion': [C.POINTER(C.c_char)],
            'CONFIG_Preset': [], 'CONFIG_SetCenterFreq': [C.c_double],
            'CONFIG_SetReferenceLevel': [C.c_double], 'CONFIG_GetReferenceLevel': [C.POINTER(C.c_double)],
            'SPECTRUM_SetEnable': [C.c_bool], 'SPECTRUM_SetDefault': [],
            'SPECTRUM_GetSettings': [C.POINTER(Settings)], 'SPECTRUM_SetSettings': [Settings],
            'SPECTRUM_SetTraceType': [C.c_int, C.c_bool, C.c_int],
            'DEVICE_Run': [], 'DEVICE_Stop': [], 'SPECTRUM_AcquireTrace': [],
            'SPECTRUM_GetTrace': [C.c_int, C.c_int, C.POINTER(C.c_float), C.POINTER(C.c_int)],
            'SPECTRUM_GetTraceInfo': [C.POINTER(TraceInfo)],
        }
        self.wait_name = next((n for n in ('SPECTRUM_WaitForDataReady', 'SPECTRUM_WaitForTraceReady') if hasattr(dll,n)), None)
        if self.wait_name is None: raise RuntimeError('Installed RSA_API.dll lacks spectrum wait support')
        signatures[self.wait_name] = [C.c_int, C.POINTER(C.c_bool)]
        for name, types in signatures.items():
            try: fn = getattr(dll, name)
            except AttributeError: raise RuntimeError(f'Installed RSA_API.dll lacks {name}') from None
            fn.argtypes, fn.restype = types, C.c_int
        assert C.sizeof(Settings) == 96 and Settings.actualNumIQSamples.offset == 88
        assert C.sizeof(TraceInfo) == 16

    def call(self, name, *args):
        status = int(getattr(self.dll, name)(*args))
        if status: raise RuntimeError(f'{name} returned Tektronix API status {status}')

    def text(self, name):
        out = C.create_string_buffer(100)
        self.call(name, out)
        return out.value.decode('utf-8', errors='replace')

    def search(self):
        count = C.c_int()
        ids = (C.c_int * 20)()
        serials, models = C.create_string_buffer(2000), C.create_string_buffer(2000)
        self.call('DEVICE_Search', C.byref(count), ids, serials, models)
        if not 0 <= count.value <= 20: raise RuntimeError('Invalid device count from RSA API')
        return list(ids[:count.value])

    def identity(self):
        result = {'type':'device', 'schema_version':1, 'backend':'tektronix-windows-api',
                  'calibration_basis':'vendor_api', 'absolute_accuracy_verified':False}
        for key, name in [('model','DEVICE_GetNomenclature'),('serial','DEVICE_GetSerialNumber'),
                          ('hardware','DEVICE_GetHWVersion'),('firmware','DEVICE_GetFWVersion'),
                          ('fpga','DEVICE_GetFPGAVersion'),('api_version','DEVICE_GetAPIVersion')]:
            result[key] = self.text(name)
        if result['model'] != 'RSA306B' or not result['serial']: raise RuntimeError('This plugin supports RSA306B only')
        return result

    def configure(self, segment, config, defaults):
        settings = Settings.from_buffer_copy(defaults)
        settings.span = segment['stop_hz']-segment['start_hz']
        settings.rbw, settings.enableVBW = config.rbw, False
        settings.traceLength, settings.window, settings.verticalUnit = TRACE_LENGTH, 0, 0  # Kaiser; dBm
        self.call('CONFIG_SetCenterFreq', (segment['start_hz']+segment['stop_hz'])/2)
        self.call('CONFIG_SetReferenceLevel', config.reference)
        self.call('SPECTRUM_SetSettings', settings)
        self.call('SPECTRUM_SetTraceType', 0, True, 0)  # Positive peak; SDK handles hold/average.
        self.call('SPECTRUM_GetSettings', C.byref(settings))
        ref = C.c_double(); self.call('CONFIG_GetReferenceLevel', C.byref(ref))
        actual = {name:getattr(settings,name) for name,_ in settings._fields_}
        actual['reference_dbm'] = ref.value
        validate_settings(actual)
        return actual


def validate_settings(s):
    if not all(math.isfinite(v) for v in s.values()): raise RuntimeError('Non-finite API settings')
    if not 2 <= s['traceLength'] <= 64001 or s['verticalUnit'] != 0 or s['actualFreqStepSize'] <= 0 or s['actualRBW'] <= 0:
        raise RuntimeError('Invalid API axis, trace length, RBW or dBm unit')
    if s['actualNumIQSamples'] <= 0: raise RuntimeError('Invalid API sample count')


@contextmanager
def exclusive():
    kernel = C.WinDLL('kernel32', use_last_error=True)
    kernel.CreateMutexW.argtypes = [C.c_void_p, C.c_bool, C.c_wchar_p]; kernel.CreateMutexW.restype = C.c_void_p
    kernel.WaitForSingleObject.argtypes = [C.c_void_p, C.c_uint32]; kernel.WaitForSingleObject.restype = C.c_uint32
    kernel.ReleaseMutex.argtypes = [C.c_void_p]; kernel.CloseHandle.argtypes = [C.c_void_p]
    handle = kernel.CreateMutexW(None, False, 'Global\\RSA306B-SoundBase-Tektronix-API-v1')
    if not handle: raise OSError(C.get_last_error(), 'Cannot create RSA ownership mutex')
    owned = False
    try:
        status = kernel.WaitForSingleObject(handle, 0)
        if status not in (0, 0x80): raise RuntimeError('RSA API is busy in another plugin worker')
        owned = True
        yield
    finally:
        if owned: kernel.ReleaseMutex(handle)
        kernel.CloseHandle(handle)


def locate_api(explicit=None):
    if explicit:
        candidates = [Path(explicit)]
    else:
        pf = Path(os.environ.get('ProgramFiles', 'C:/Program Files'))
        candidates = [Path('C:/Tektronix/RSA_API/lib/x64/RSA_API.dll'),
                      pf/'Tektronix/RSA_API/lib/x64/RSA_API.dll',
                      pf/'Tektronix/RSA_API/RSA_API.dll',
                      pf/'Tektronix/SignalVu-PC/RSA/HALSV/DigitizerSourceFactories/RSA_API.dll']
    for path in candidates:
        if not path.is_absolute(): raise ValueError('Windows API path must be absolute')
        if path.is_dir(): path = path/'RSA_API.dll'
        if path.is_file():
            dirs = [path.parent]
            if path.parent.name == 'DigitizerSourceFactories': dirs.append(path.parents[2])
            return path, list(dict.fromkeys(dirs))
    raise RuntimeError('Tektronix RSA API was not found. Install or repair the official 64-bit RSA API and USB driver using the standard installation location; the plugin detects the API automatically.')


@contextmanager
def load_api(explicit=None):
    if sys.platform != 'win32' or struct.calcsize('P') != 8: raise RuntimeError('Windows x64 is required')
    path, dirs = locate_api(explicit)
    with ExitStack() as stack:
        for directory in dirs: stack.enter_context(os.add_dll_directory(str(directory)))
        dll = C.CDLL(str(path))
        yield Api(dll)


@contextmanager
def connect(api, serial=None):
    ids = api.search()
    if len(ids) != 1: raise RuntimeError(f'Connect exactly one RSA306B; API found {len(ids)} analyzers')
    attempted = False
    try:
        attempted = True
        api.call('DEVICE_Connect',ids[0])
        identity = api.identity()
        if serial and identity['serial'] != serial: raise RuntimeError('Connected RSA differs from the selected analyzer')
        yield identity
    finally:
        if attempted: api.call('DEVICE_Disconnect')


def prepare(api):
    api.call('CONFIG_Preset')
    api.call('SPECTRUM_SetEnable',True)
    api.call('SPECTRUM_SetDefault')
    defaults = Settings()
    api.call('SPECTRUM_GetSettings',C.byref(defaults))
    return bytes(defaults)


class Cancelled(Exception): pass


def acquire(api, settings, stop):
    begin, mono = time.time(), time.monotonic()
    try:
        api.call('DEVICE_Run')
        api.call('SPECTRUM_AcquireTrace')
        ready = C.c_bool(False)
        while not ready.value:
            if stop.is_set(): raise Cancelled()
            if time.monotonic()-mono > 10: raise TimeoutError('Timed out waiting for Tektronix spectrum')
            api.call(api.wait_name,100,C.byref(ready))
        if stop.is_set(): raise Cancelled()
        length = settings['traceLength']; values = (C.c_float * length)(); actual = C.c_int()
        api.call('SPECTRUM_GetTrace',0,length,values,C.byref(actual))
        if actual.value != length: raise RuntimeError('Partial API spectrum; sweep discarded')
        info = TraceInfo(); api.call('SPECTRUM_GetTraceInfo',C.byref(info))
        levels = list(values)
        if not all(math.isfinite(x) for x in levels): raise RuntimeError('Non-finite API spectrum; sweep discarded')
        return {'levels':levels,'host_begin':begin,'host_end':time.time(),
                'device_timestamp_ticks':str(info.timestamp),'raw_status':int(info.acqDataStatus)}
    finally:
        api.call('DEVICE_Stop')


def reduce_segment(config, settings, levels, output):
    step = (config.stop-config.start)/(config.points-1)
    for i, level in enumerate(levels):
        freq = settings['actualStartFreq'] + i*settings['actualFreqStepSize']
        if config.start <= freq <= config.stop:
            index = math.floor((freq-config.start)/step+.5)
            if 0 <= index < config.points:
                output[index] = level if output[index] is None else max(output[index],level)


def run_sweeps(api, identity, config, count, stop, send=emit):
    segments = plan(config); defaults = prepare(api)
    previous_ticks = None
    for sequence in range(count):
        if stop.is_set(): return
        begin, mono = time.time(), time.monotonic()
        values = [None]*config.points; timing = []; bad = False; last_end = None
        for segment in segments:
            if stop.is_set(): return
            settings = api.configure(segment,config,defaults)
            trace = acquire(api,settings,stop)
            ticks = int(trace['device_timestamp_ticks'])
            regressed = previous_ticks is not None and ticks <= previous_ticks
            previous_ticks = ticks
            bad |= trace['raw_status'] != 0 or regressed
            reduce_segment(config,settings,trace['levels'],values)
            timing.append({'host_begin':trace['host_begin'],'host_end':trace['host_end'],
                           'device_timestamp_ticks':trace['device_timestamp_ticks'],
                           'raw_status':trace['raw_status'],'timestamp_regressed':regressed,
                           'gap_seconds':None if last_end is None else trace['host_begin']-last_end,
                           'actual_start_hz':settings['actualStartFreq'],
                           'actual_step_hz':settings['actualFreqStepSize'],'trace_length':settings['traceLength'],
                           'actual_rbw_hz':settings['actualRBW'],'reference_dbm':settings['reference_dbm']})
            last_end = trace['host_end']
            send({'type':'progress','schema_version':1,'sequence':sequence,'segments_completed':len(timing),'segment_count':len(segments)})
        missing = sum(x is None for x in values)
        common = {'schema_version':1,'sequence':sequence,'serial':identity['serial'],
                  'api_version':identity['api_version'],'start_hz':config.start,'stop_hz':config.stop,
                  'point_count':config.points,'requested_rbw_hz':config.rbw,'reference_dbm':config.reference,
                  'host_begin':begin,'duration_seconds':time.monotonic()-mono,'segments':timing,
                  'calibration_basis':'vendor_api','absolute_accuracy_verified':False,'level_unit':'dBm'}
        if bad or missing:
            send({'type':'gap',**common,'reason':'API quality flags or unobserved output cells','missing_cells':missing})
        else:
            send({'type':'sweep',**common,'complete':True,'values':values})


class MockApi(Api):
    """Explicit SDK substitute: exercise the real ctypes/acquisition/reduction path."""
    def __init__(self):
        self.wait_name = 'SPECTRUM_WaitForDataReady'
        self.settings = Settings()
        self.settings.traceLength = TRACE_LENGTH
        self.settings.actualNumIQSamples = 4096
        self.center = 582e6
        self.reference = -20
        self.ticks = 0

    def text(self, name):
        return {'DEVICE_GetAPIVersion':'mock-1', 'DEVICE_GetSerialNumber':'MOCK-RSA306B',
                'DEVICE_GetNomenclature':'RSA306B', 'DEVICE_GetHWVersion':'7',
                'DEVICE_GetFWVersion':'mock', 'DEVICE_GetFPGAVersion':'mock'}[name]

    def search(self): return [1]

    def identity(self): return {**super().identity(), 'synthetic':True}

    def call(self, name, *args):
        if name == 'CONFIG_SetCenterFreq': self.center = float(args[0])
        elif name == 'CONFIG_SetReferenceLevel': self.reference = float(args[0])
        elif name == 'CONFIG_GetReferenceLevel': C.cast(args[0], C.POINTER(C.c_double))[0] = self.reference
        elif name == 'SPECTRUM_SetSettings':
            self.settings = Settings.from_buffer_copy(args[0])
            self.settings.actualStartFreq = self.center-self.settings.span/2
            self.settings.actualStopFreq = self.center+self.settings.span/2
            self.settings.actualFreqStepSize = self.settings.span/(self.settings.traceLength-1)
            self.settings.actualRBW = self.settings.rbw
            self.settings.actualNumIQSamples = 4096
        elif name == 'SPECTRUM_GetSettings': C.memmove(args[0], C.byref(self.settings), C.sizeof(Settings))
        elif name == self.wait_name:
            time.sleep(.002)
            C.cast(args[1], C.POINTER(C.c_bool))[0] = True
        elif name == 'SPECTRUM_GetTrace':
            for i in range(args[1]):
                f = self.settings.actualStartFreq+i*self.settings.actualFreqStepSize
                args[2][i] = -100 + 60*math.exp(-((f-550e6)/2e6)**2)
            C.cast(args[3], C.POINTER(C.c_int))[0] = args[1]
        elif name == 'SPECTRUM_GetTraceInfo':
            self.ticks += 1
            info = C.cast(args[0], C.POINTER(TraceInfo)).contents
            info.timestamp, info.acqDataStatus = self.ticks, 0


@contextmanager
def mock_api():
    yield MockApi()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=['capabilities','discover','probe','plan','configure','sweep'])
    p.add_argument('--dll'); p.add_argument('--serial')
    p.add_argument('--start-hz',type=float,default=470e6); p.add_argument('--stop-hz',type=float,default=694e6)
    p.add_argument('--rbw-hz',type=float,default=10000); p.add_argument('--reference-dbm',type=float,default=-20)
    p.add_argument('--points',type=int,default=1001); p.add_argument('--count',type=int,default=1_000_000)
    p.add_argument('--stdin-control',action='store_true')
    p.add_argument('--mock',action='store_true',help='Explicit synthetic SDK for tests; never opens hardware')
    args = p.parse_args(argv)
    config = Config(args.start_hz,args.stop_hz,args.rbw_hz,args.reference_dbm,args.points)
    stop = threading.Event()
    if args.stdin_control:
        def watch():
            # Any line or EOF requests stop; only this thread touches stdin.
            sys.stdin.buffer.readline(256); stop.set()
        threading.Thread(target=watch,daemon=True).start()
    try:
        if not 1 <= args.count <= 1_000_000: raise ValueError('Invalid sweep count')
        if args.command in ('plan','configure','sweep'): config.validate()
        if args.command == 'plan':
            emit({'type':'plan','schema_version':1,'segments':plan(config)}); return 0
        with (mock_api() if args.mock else load_api(args.dll)) as api:
            if args.command == 'capabilities':
                emit({'type':'capabilities','schema_version':1,'backend':'tektronix-windows-api',
                      'api_version':api.text('DEVICE_GetAPIVersion'),'min_hz':MIN_HZ,'max_hz':MAX_HZ,
                      'min_rbw_hz':MIN_RBW,'max_rbw_hz':MAX_RBW,'calibration_basis':'vendor_api',
                      'level_unit':'dBm','absolute_accuracy_verified':False}); return 0
            with (nullcontext() if args.mock else exclusive()):
                if args.command == 'discover':
                    emit({'type':'discovery','device_count':len(api.search())}); return 0
                with connect(api,args.serial) as identity:
                    if args.command == 'probe': emit(identity); return 0
                    if args.command == 'configure':
                        defaults = prepare(api); settings = []
                        for segment in plan(config): settings.append(api.configure(segment,config,defaults))
                        emit({'type':'configuration','serial':identity['serial'],'segments':settings}); return 0
                    emit(identity)
                    try: run_sweeps(api,identity,config,args.count,stop)
                    except Cancelled: pass
        if args.command == 'sweep': emit({'type':'stopped','schema_version':1,'reason':'requested' if stop.is_set() else 'count'})
        return 0
    except Exception as error:
        print(json.dumps({'type':'error','message':str(error)},ensure_ascii=True),file=sys.stderr,flush=True)
        return 1


if __name__ == '__main__': raise SystemExit(main())

#!/usr/bin/env python3
"""Decode observed RSA306B command envelopes and raw IF footers; never replay USB.

See docs/USBProtocol.md for evidence, revision limits, and field meanings.
Optional extraction writes contiguous signed int16 LE ADC samples, not complex IQ.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
from inspect_usbpcap import packets

FRAME_BYTES, SAMPLE_COUNT = 16384, 8178
COMMANDS = {1:'run',2:'stop',3:'firmware',4:'read_registers',5:'write_registers',11:'controller_mode',
            17:'read_block_header',19:'read_block_page',22:'echo',23:'post'}

def envelope(data: bytes, response=False):
    minimum=12 if response else 8
    if len(data)<minimum or len(data)%2:
        raise ValueError('Incomplete command envelope')
    seq,length,opcode=struct.unpack_from('<HHH',data)
    if length!=len(data) or data[-2:]!=b'\xff\xff':
        raise ValueError('Command length/terminator mismatch')
    result={'sequence':seq,'opcode':opcode,'name':COMMANDS.get(opcode,'unknown')}
    if response:
        result.update(status=struct.unpack_from('<H',data,6)[0],auxiliary=struct.unpack_from('<H',data,8)[0])
    payload=data[10 if response else 6:-2]
    result['payload_hex']=payload.hex()
    if opcode==5 and not response:
        if len(payload)<2:raise ValueError('Missing register count')
        count=struct.unpack_from('<H',payload)[0]
        if len(payload)!=2+count*4:raise ValueError('Register count mismatch')
        result['registers']=[{'address':a,'value':v} for a,v in struct.iter_unpack('<HH',payload[2:])]
    return result

def footer(data: bytes):
    if len(data)!=FRAME_BYTES:raise ValueError('IF frame must be 16384 bytes')
    tag,counter,t2,t1,sync,status,tick=struct.unpack_from('<HIHHHHQ',data,16362)
    return dict(descriptor=tag,counter=counter,trigger2_index=t2,trigger1_index=t1,
                time_sync_index=sync,status=status,timestamp_ticks=str(tick))

def decode(path: Path, device=None, command_sink=None, frame_sink=None, extract_tag=None, sample_sink=None):
    if extract_tag is not None and (not 0<=extract_tag<65535 or sample_sink is None):
        raise ValueError('Extraction needs a valid non-transition tag and a sample sink')
    pending={}; counts=Counter();frame_counts=Counter();frames=0;remainder={};previous={};gaps=0
    extracted=0;extract_previous=None;first=None;last=None;selected_devices=set()
    digest=hashlib.sha256()
    for p in packets(path):
        if device is not None and p['device']!=device:continue
        if p['endpoint'] not in (3,0x83,0x84):continue
        if p['included']!=p['original'] or len(p['payload'])!=p['data_length']:
            raise ValueError(f"Truncated packet {p['number']}")
        b=p['payload'];complete=bool(p['info']&1);key=(p['bus'],p['device'])
        if p['endpoint']==0x84 and complete and p['status']:
            counts['failed_bulk_completions']+=1
            counts['discarded_failed_bulk_bytes']+=len(b)
            continue
        if not b:continue
        if p['endpoint']==3 and not complete:
            request=envelope(b);seq=request['sequence']
            if key in pending:raise ValueError('Request without response before next command')
            pending[key]=(request,p['number']);counts[request['name']]+=1
        elif p['endpoint']==0x83 and complete:
            if p['status']:raise ValueError('Failed command response transfer')
            reply=envelope(b,True)
            if key not in pending:raise ValueError('Unpaired command response')
            req,num=pending.pop(key)
            if (req['sequence'],req['opcode'])!=(reply['sequence'],reply['opcode']):raise ValueError('Unmatched response')
            record=dict(request_packet=num,response_packet=p['number'],bus=p['bus'],device=p['device'],
                        request=req,response=reply)
            if command_sink:command_sink.write(json.dumps(record)+'\n')
            if reply['status'] or reply['auxiliary']:raise ValueError('Device command error')
        elif p['endpoint']==0x84 and complete:
            # Canceled transfers often contain partial buffers. Report them separately; do not turn them into RF data.
            selected_devices.add(key)
            if extract_tag is not None and len(selected_devices)>1:raise ValueError('Select one USB device for sample extraction')
            data=remainder.get(key,b'')+b
            offset=0
            while len(data)-offset>=FRAME_BYTES:
                raw=data[offset:offset+FRAME_BYTES];f=footer(raw);offset+=FRAME_BYTES;frames+=1
                frame_counts[str(f['descriptor'])]+=1
                old=previous.get(key)
                contiguous=old is None or (f['descriptor']==old['descriptor'] and
                    f['counter']==(old['counter']+1)&0xffffffff and
                    int(f['timestamp_ticks'])==int(old['timestamp_ticks'])+SAMPLE_COUNT)
                if old and f['descriptor']==old['descriptor'] and not contiguous:gaps+=1
                f.update(packet=p['number'],bulk_stream_frame=frames-1,bus=p['bus'],device=p['device'],contiguous_with_previous=contiguous)
                previous[key]=f
                if frame_sink:frame_sink.write(json.dumps(f)+'\n')
                if f['descriptor']==extract_tag:
                    if extract_previous and (f['counter']!=(extract_previous['counter']+1)&0xffffffff or
                        int(f['timestamp_ticks'])!=int(extract_previous['timestamp_ticks'])+SAMPLE_COUNT):
                        raise ValueError('Selected tag has a sample gap; refusing to concatenate across it')
                    samples=raw[:16356];sample_sink.write(samples);digest.update(samples)
                    first=f if first is None else first;last=f;extract_previous=f;extracted+=SAMPLE_COUNT
            remainder[key]=data[offset:]
    if pending:raise ValueError('Unanswered command at end of capture')
    if any(remainder.values()):raise ValueError('Successful bulk stream ends in an incomplete IF frame')
    if extract_tag is not None and not extracted:raise ValueError('Selected frame tag was not found')
    return dict(schema_version=1,pcap=str(path),command_counts=dict(counts),if_frames=frames,
                frame_tags=dict(frame_counts),within_tag_gap_events=gaps,sample_rate_hz=112000000,
                sample_format='signed-int16-le-real-adc',calibrated=False,
                extracted_samples=extracted,extracted_sha256=digest.hexdigest() if extracted else None,
                first_extracted_frame=first,last_extracted_frame=last)

def main():
    from contextlib import ExitStack
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('pcap',type=Path)
    p.add_argument('--device',type=int);p.add_argument('--commands',type=Path);p.add_argument('--frames',type=Path)
    p.add_argument('--extract-tag',type=int);p.add_argument('--output-if',type=Path)
    a=p.parse_args()
    if (a.extract_tag is None)!=(a.output_if is None):p.error('Use --extract-tag and --output-if together')
    if a.extract_tag is not None and not 0<=a.extract_tag<65535:p.error('Extraction tag must be 0…65534')
    if a.device is not None and not 0<=a.device<=127:p.error('USB address must be 0…127')
    created_sample=False;success=False
    try:
        with ExitStack() as stack:
            c=stack.enter_context(a.commands.open('x')) if a.commands else None
            f=stack.enter_context(a.frames.open('x')) if a.frames else None
            s=stack.enter_context(a.output_if.open('xb')) if a.output_if else None
            created_sample=s is not None
            result=decode(a.pcap,a.device,c,f,a.extract_tag,s)
        success=True
        print(json.dumps(result,indent=2));return 0
    except (ValueError,OSError) as e:
        p.exit(1,str(e)+'\n')
    finally:
        if created_sample and not success:a.output_if.unlink(missing_ok=True)
if __name__=='__main__':raise SystemExit(main())

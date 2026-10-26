import json
from pathlib import Path
import struct
import sys
import unittest
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_calibration import BlockCollector, crc16, describe, native_payloads


def message(opcode, request, response, device=1):
    return json.dumps(dict(bus=1, device=device, request=dict(opcode=opcode, payload_hex=request.hex()),
                          response=dict(status=0, auxiliary=0, payload_hex=response.hex())))


def if_table():
    return struct.pack('<4I64f', 0, 32, 16, 144, *range(-11,21), *range(32))


class CalibrationInspectionTests(unittest.TestCase):
    def test_native_dump_crc_identity_completeness_and_bounds(self):
        identity = b'RSA306B'.ljust(256,b'\0')+b'TEST'.ljust(256,b'\0')
        blocks = [(i, identity if i == 0 else bytes(16)) for i in (0,1,2,3,4,5,6,8)]
        dump = dict(schema_version=1, type='calibration_dump', calibrated=False, model='RSA306B',
                    serial='TEST', hardware=7, firmware_word=0x701, fpga_word=0x201,
                    blocks=[dict(index=i,bytes=len(data),crc16=crc16(data),payload_hex=data.hex()) for i,data in blocks])
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/'dump.json'
            path.write_text(json.dumps(dump))
            self.assertEqual([i for i,_,_ in native_payloads(path)], [0,1,2,3,4,5,6,8])
            for invalid_root in (None, [], 'invalid', 1):
                path.write_text(json.dumps(invalid_root))
                with self.assertRaisesRegex(ValueError, 'Unsupported native'):
                    native_payloads(path)
            for field, value in (('serial','WRONG'), ('hardware',8), ('blocks',dump['blocks'][:-1])):
                bad = dict(dump);bad[field]=value;path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):native_payloads(path)
            for field, value in (('crc16',1), ('bytes',100000), ('index',0), ('payload_hex','xx'*16)):
                bad=json.loads(json.dumps(dump));bad['blocks'][1][field]=value;path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):native_payloads(path)

    def test_crc_and_complete_block(self):
        self.assertEqual(crc16(b'123456789'), 0xbb3d)
        data = if_table(); c = BlockCollector()
        c.write(message(17, struct.pack('<H',3), struct.pack('<HIH', crc16(data), len(data), 0)))
        for i in (1, 0):
            c.write(message(19, struct.pack('<HH',3,i), data[i*256:(i+1)*256].ljust(256,b'\0')))
        self.assertEqual(list(c.payloads()), [(3,crc16(data),data)])
        self.assertEqual(describe(3,data)['settings'], list(range(-11,21)))
        self.assertFalse(describe(3,data)['calibrated'])

    def test_missing_crc_conflict_and_multiple_devices(self):
        c=BlockCollector();data=b'abc'
        c.write(message(17,struct.pack('<H',1),struct.pack('<HIH',crc16(data),3,0)))
        with self.assertRaisesRegex(ValueError,'Incomplete'):list(c.payloads())
        c.write(message(19,struct.pack('<HH',1,0),b'bad'.ljust(256,b'\0')))
        with self.assertRaisesRegex(ValueError,'CRC'):list(c.payloads())
        with self.assertRaisesRegex(ValueError,'Conflicting'):
            c.write(message(19,struct.pack('<HH',1,0),data.ljust(256,b'\0')))
        with self.assertRaisesRegex(ValueError,'one USB device'):
            c.write(message(17,struct.pack('<H',1),struct.pack('<HIH',0,3,0),device=2))

    def test_bad_table_bounds_revision_and_values(self):
        for offset,value in ((0,9),(4,31),(8,0),(12,268)):
            data=bytearray(if_table());struct.pack_into('<I',data,offset,value)
            with self.assertRaises(ValueError):describe(3,data)
        data=bytearray(if_table());struct.pack_into('<f',data,16,float('nan'))
        with self.assertRaisesRegex(ValueError,'Non-finite'):describe(3,data)
        with self.assertRaises(ValueError):describe(3,b'\0')

    def test_rf_table_shapes_and_frequency_order(self):
        data=struct.pack('<6I2f3d6f',0,2,3,24,32,56,0,2,1e6,2e6,3e6,10,11,12,8,9,10)
        result=describe(4,data)
        self.assertEqual(result['measured_rows'],[[10,11,12],[8,9,10]])
        bad=bytearray(data);struct.pack_into('<d',bad,40,1e6)
        with self.assertRaisesRegex(ValueError,'Non-increasing'):describe(4,bad)
        descriptor=struct.pack('<dd24x',0,700e6)
        data=struct.pack('<10I',1,40,1,80,2,96,4,2,0,128)+descriptor+struct.pack('<2d8f',1e6,2e6,*range(8))
        result=describe(5,data)
        self.assertEqual(result['gain_rows'],[[0,1],[2,3],[4,5],[6,7]])
        self.assertEqual(result['descriptor_ranges_hz'],[(0,700e6)])

    def test_lf_and_response_tables(self):
        data=struct.pack('<9I',0,2,2,3,36,44,52,60,84)+struct.pack('<4f2I3d6f',0,1,2,3,31,199,1e3,2e3,3e3,*range(6))
        result=describe(2,data)
        self.assertEqual(result['vga_ids'],[31,199])
        self.assertEqual(result['measured_rows'],[[0,1,2],[3,4,5]])
        data=struct.pack('<5I',1,2,3,20,100)+bytes(80)+struct.pack('<6f',*range(6))
        self.assertEqual(describe(6,data)['response_rows'],[[0,1,2],[3,4,5]])
        self.assertEqual(describe(8,data)['kind'],'normalized-if-response')
        self.assertEqual(describe(7,b'anything'),{'layout':'not interpreted'})

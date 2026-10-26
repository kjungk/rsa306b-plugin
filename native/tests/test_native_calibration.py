"""Native conversion checks against an analytic synthetic factory, without USB."""
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


def tables(root):
    b=bytearray(3420)
    struct.pack_into('<9I',b,0,0,46,2,188,36,220,404,412,1916)
    settings=[float(i-11) for i in range(46)]
    settings[40]=20*math.log10((1+6.079458)*.055744*71)
    struct.pack_into('<46f',b,36,*settings);struct.pack_into('<46f',b,220,*settings)
    struct.pack_into('<2I',b,404,6,35)
    lf=[9000,10000,15000]+[20000+i*(45e6-20000)/184 for i in range(185)]
    struct.pack_into('<188d',b,412,*lf);struct.pack_into('<376f',b,1916,*([-8]*376))
    (root/'block-2.bin').write_bytes(b)
    n=1264
    f=[1e6+i*5e6 for i in range(n)]
    settings=list(range(-11,21))
    (root/'block-3.bin').write_bytes(struct.pack('<4I64f',0,32,16,144,*settings,*[v*1.1 for v in settings]))
    a=[0,2,6,10,14,18,22,26,31]
    (root/'block-4.bin').write_bytes(struct.pack('<6I9f',0,9,n,24,60,60+n*8,*a)+struct.pack('<1264d',*f)+struct.pack('<11376f',*[-v for v in a for _ in f]))
    b=bytearray(30856)
    struct.pack_into('<10I',b,0,1,40,12,520,n,520+n*8,4,n,0,len(b))
    for i in range(12):struct.pack_into('<dd',b,40+i*40,i*600e6,(i+1)*600e6)
    struct.pack_into('<1264d',b,520,*f)
    struct.pack_into('<5056f',b,520+n*8,*[row*5 for row in range(4) for _ in f])
    (root/'block-5.bin').write_bytes(b)
    (root/'block-6.bin').write_bytes(struct.pack('<5I',1,2,501,20,100)+bytes(80)+struct.pack('<1002f',*([3]*501+[7]*501)))


@unittest.skipUnless(os.environ.get('RSA_CALIBRATION_CHECK'),'Run via CTest')
class NativeCalibrationTests(unittest.TestCase):
    def call(self,root,line='594000000 594000000 113 2 10 2440000000 1\n'):
        return subprocess.run([os.environ['RSA_CALIBRATION_CHECK'],str(root)],input=line,text=True,capture_output=True,timeout=5)

    def test_analytic_gain_units_and_flat_response(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);tables(root)
            result=self.call(root);self.assertEqual(result.returncode,0,result.stderr)
            r=json.loads(result.stdout)
            self.assertAlmostEqual(r['gain_db'],19,places=6)
            self.assertAlmostEqual(r['volts_per_adc_count'],10**(-19/20)/32767,places=12)
            self.assertEqual(r['eq_amplitude_db'],[0]*501)
            # 1 V peak into 50 ohms is 10 dBm. Signed-count normalization is explicit.
            base=10+20*math.log10(32768/32767)-19
            self.assertAlmostEqual(r['test_levels_dbm'][0],base,places=6)
            self.assertAlmostEqual(r['test_levels_dbm'][1],base-20*math.log10(2),places=6)

    def test_lf_gain_and_direct_frequency_power_units(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);tables(root)
            result=self.call(root,'10000000 10000000 80 2 10 0 0 199\n')
            self.assertEqual(result.returncode,0,result.stderr)
            row=json.loads(result.stdout)
            setting=struct.unpack('<f',struct.pack('<f',20*math.log10((1+6.079458)*.055744*71)))[0]
            self.assertAlmostEqual(row['gain_db'],setting-8,places=6)
            self.assertAlmostEqual(row['volts_per_adc_count'],10**(-(setting-8)/20)/32767,places=12)
            self.assertEqual(len(row['eq_amplitude_db']),401)
            self.assertAlmostEqual(row['test_levels_dbm'][0],10+20*math.log10(32768/32767)-setting+8,places=6)
            self.assertAlmostEqual(row['test_levels_dbm'][1]-row['test_levels_dbm'][0],-20*math.log10(2),places=6)

    def test_reject_corrupt_tables_and_unsupported_gain(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name)
            for block,offset,fmt,value in ((2,0,'I',1),(2,4,'I',45),(2,24,'I',0),(2,1916,'f',float('nan')),(3,0,'I',9),(4,16,'I',0),(5,24,'I',5),(6,4,'I',3),(5,520+8,'d',1e6),(3,144,'f',float('nan'))):
                tables(root);path=root/f'block-{block}.bin';b=bytearray(path.read_bytes());struct.pack_into('<'+fmt,b,offset,value);path.write_bytes(b)
                self.assertNotEqual(self.call(root).returncode,0)
            tables(root)
            for bits,atten,gain in ((112,2,10),(113,31.5,10),(113,2,-11.5)):
                self.assertNotEqual(self.call(root,f'594000000 594000000 {bits} {atten} {gain} 2440000000 1\n').returncode,0)
            # A well-ordered but displaced axis must fail before index arithmetic.
            tables(root);path=root/'block-5.bin';b=bytearray(path.read_bytes())
            struct.pack_into('<1264d',b,520,*[1e9+i*5e6 for i in range(1264)]);path.write_bytes(b)
            self.assertNotEqual(self.call(root).returncode,0)
            # Tiny first spacing must not cause an effectively unbounded extension loop.
            tables(root);b=bytearray(path.read_bytes());struct.pack_into('<d',b,528,1e6+1e-6);path.write_bytes(b)
            self.assertNotEqual(self.call(root,'23000000 23000000 113 2 10 2440000000 1\n').returncode,0)

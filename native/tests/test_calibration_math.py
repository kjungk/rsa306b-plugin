import math
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from calibration_math import FactoryModel, linear, pchip, rf_config, spline


def model():
    # Synthetic calibration with simple, independently calculable path gains.
    freq = [i*1e6 for i in range(1, 101)]
    return FactoryModel({
        2: dict(settings=list(range(-11, 35)), measured_values=list(range(46)),
                frequency_hz=[9000, 10000, 15000, 1e6, 45e6], measured_rows=[[-9]*5, [-8]*5]),
        3: dict(settings=list(range(-11, 21)), measured_values=[2*i for i in range(-11, 21)]),
        4: dict(settings=[0, 2, 31], frequency_hz=[1e6, 100e6], measured_rows=[[7, 8], [5, 6], [-24, -23]]),
        5: dict(frequency_hz=freq, gain_rows=[[i]*len(freq) for i in (-10, 0, 10, 20)],
                descriptor_ranges_hz=[(0, 50e6), (50e6, 101e6)]),
        6: dict(response_rows=[[i/100 for i in range(501)], [i/200 for i in range(501)]])})


class CalibrationMathTests(unittest.TestCase):
    def test_pchip_shape_and_nonuniform_linear_grid(self):
        self.assertEqual(pchip([0, 1, 2], [0, 1, 4], [0, .5, 1, 2]), [0, .3125, 1, 4])
        result = pchip([0, 1, 3, 9], [2, 5, 11, 29], [-1, .25, 2, 7, 10])
        for a, b in zip(result, [-1, 2.75, 8, 23, 32]):
            self.assertAlmostEqual(a, b)
        plateau = pchip([0, 1, 2, 3], [0, 1, 1, 2], [1.25, 1.5, 1.75])
        self.assertEqual(plateau, [1, 1, 1])

    def test_natural_spline_is_not_the_hermite_curve(self):
        # Natural spline through y=|x| at -1, 0, 1 has y(+-0.5)=0.3125.
        self.assertEqual(spline([-1, 0, 1], [1, 0, 1], [-.5, 0, .5]), [.3125, 0, .3125])
        self.assertNotEqual(pchip([-1, 0, 1], [1, 0, 1], [.5]), [.3125])

    def test_reject_bad_axes_targets_and_extrapolated_gain_settings(self):
        for function in (pchip, spline):
            for x, y, v in (([0, 0], [1, 2], [0]), ([0, 1], [1], [0]),
                            ([0, 1], [1, math.nan], [0]), ([0, 1], [1, 2], [math.inf])):
                with self.assertRaises(ValueError): function(x, y, v)
        with self.assertRaises(ValueError): linear([0, 2], [0, -2], 2.5)
        with self.assertRaises(ValueError): model().scale(40e6, 0x71, 31.5, 11)
        with self.assertRaises(ValueError): model().scale(40e6, 0x71, 1, -11.5)

    def test_rf_routes_and_count_scaling(self):
        self.assertEqual([rf_config(v) for v in (0x50, 0x51, 0x71, 0x79)], [0, 1, 2, 3])
        with self.assertRaisesRegex(ValueError, 'no calibrated row'): rf_config(0x70)
        # Path +10, one-dB attenuation, nominal IF +1 with measured gain +2.
        self.assertEqual(model().total_gain(40e6, 0x71, 1, 1), 11)
        self.assertAlmostEqual(model().scale(40e6, 0x71, 1, 1), 10**(-11/20)/32767)
        # A full-scale peak sine is 1 V / sqrt(2), or 10 dBm into 50 ohms.
        self.assertAlmostEqual(10*math.log10(1/2/50*1000), 10)
        self.assertAlmostEqual(20*math.log10(32768/32767), .0002650763603796191)

    def test_eq_route_orientation_and_if_selection(self):
        f = [3e6+i*100000 for i in range(501)]
        m = model()
        high = m.rf_eq(40e6, 40e6, 0x71, 1190e6, True, f)
        low = m.rf_eq(40e6, 40e6, 0x71, 1190e6, False, f)
        alternate = m.rf_eq(40e6, 40e6, 0x71, 2440e6, True, f)
        self.assertEqual(high[250], 0)
        self.assertAlmostEqual(high[0], 2.5)
        self.assertAlmostEqual(low[0], -2.5)
        self.assertAlmostEqual(alternate[0], 1.25)
        with self.assertRaises(ValueError): m.rf_eq(40e6, 40e6, 0x71, 1190e6, True, f[:-1])

    def test_missing_tables_and_unmeasured_lf_code(self):
        with self.assertRaises(ValueError): FactoryModel({})
        for code in (0, 128, 256, 199):
            # This synthetic LF table deliberately has no measured AD8370 code 199.
            with self.assertRaises(ValueError): model().total_gain(10e6, 0x50, 1, 11, code)

    def test_lf_off_grid_normalization_uses_natural_spline(self):
        frequencies=[-45e6+i*225000 for i in range(401)]
        center=9500
        response=model().lf_eq(center,199,frequencies)
        self.assertAlmostEqual(spline(frequencies,response,[center])[0],0,places=12)
        self.assertGreater(abs(pchip(frequencies,response,[center])[0]),1e-4)

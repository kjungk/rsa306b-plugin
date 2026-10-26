"""Offline model of the observed HW7 factory conversion, not a measurement API.

This research model is the numerical oracle for the native RF implementation.
Independent RF accuracy remains unverified. Inputs are CRC-checked tables from inspect_calibration.describe().
See docs/Calibration.md for evidence, limits and the remaining calibration gate.
"""
from bisect import bisect_left, bisect_right
import math
import struct


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def axis(x, y):
    if len(x) != len(y) or len(x) < 2 or not all(math.isfinite(v) for v in (*x, *y)):
        raise ValueError('Invalid interpolation arrays')
    if any(a >= b for a, b in zip(x, x[1:])):
        raise ValueError('Interpolation axis must increase')


def pchip(x, y, values):
    """Shape-preserving Hermite interpolation, with polynomial end extension."""
    axis(x, y)
    h = [b-a for a, b in zip(x, x[1:])]
    slope = [(b-a)/d for a, b, d in zip(y, y[1:], h)]
    derivative = [0.0] * len(x)
    if len(x) == 2:
        derivative[:] = [slope[0], slope[0]]
    else:
        def end(h0, h1, m0, m1):
            d = ((2*h0+h1)*m0-h0*m1)/(h0+h1)
            if d*m0 <= 0:
                return 0.0
            return 3*m0 if m0*m1 <= 0 and abs(d) > abs(3*m0) else d
        derivative[0] = end(h[0], h[1], slope[0], slope[1])
        derivative[-1] = end(h[-1], h[-2], slope[-1], slope[-2])
        for i in range(1, len(x)-1):
            if slope[i-1]*slope[i] > 0:
                w1, w2 = 2*h[i]+h[i-1], h[i]+2*h[i-1]
                derivative[i] = (w1+w2)/(w1/slope[i-1]+w2/slope[i])
    result = []
    for v in values:
        if not math.isfinite(v):
            raise ValueError('Non-finite interpolation target')
        i = min(max(bisect_right(x, v)-1, 0), len(x)-2)
        t = (v-x[i])/h[i]
        result.append((2*t**3-3*t**2+1)*y[i] + (t**3-2*t**2+t)*h[i]*derivative[i]
                      + (-2*t**3+3*t**2)*y[i+1] + (t**3-t**2)*h[i]*derivative[i+1])
    return result


def spline(x, y, values):
    """Natural cubic spline used by the observed low-frequency response path."""
    axis(x, y)
    n = len(x)
    second, rhs, upper = [0.0]*n, [0.0]*n, [0.0]*n
    for i in range(1, n-1):
        a, b = x[i]-x[i-1], x[i+1]-x[i]
        diagonal = 2*(a+b)-a*upper[i-1]
        upper[i] = b/diagonal
        rhs[i] = (6*((y[i+1]-y[i])/b-(y[i]-y[i-1])/a)-a*rhs[i-1])/diagonal
    for i in range(n-2, 0, -1):
        second[i] = rhs[i]-upper[i]*second[i+1]
    result = []
    for v in values:
        if not math.isfinite(v):
            raise ValueError('Non-finite interpolation target')
        i = min(max(bisect_right(x, v)-1, 0), n-2)
        h = x[i+1]-x[i]
        a, b = (x[i+1]-v)/h, (v-x[i])/h
        result.append(a*y[i]+b*y[i+1]+((a**3-a)*second[i]+(b**3-b)*second[i+1])*h*h/6)
    return result


def linear(x, y, value):
    axis(x, y)
    if not math.isfinite(value) or not x[0] <= value <= x[-1]:
        raise ValueError('Setting outside stored calibration')
    i = min(bisect_right(x, value)-1, len(x)-2)
    return y[i]+(y[i+1]-y[i])*(value-x[i])/(x[i+1]-x[i])


def rf_config(register):
    # Bit 0 is inverted by RFControl::Set20dBAttenEnable. In particular 0x70
    # combines the fixed attenuator and ME select and has no stored gain row.
    try:
        return {0x50: 0, 0x51: 1, 0x71: 2, 0x79: 3}[register]
    except KeyError:
        raise ValueError('RF switch combination has no calibrated row') from None


class FactoryModel:
    def __init__(self, tables):
        self.tables = tables
        if not all(k in tables for k in (2, 3, 4, 5, 6)):
            raise ValueError('Missing calibration tables')
        self.f = tables[5]['frequency_hz']
        self.g = tables[5]['gain_rows']
        if len(tables[6]['response_rows']) != 2 or any(len(v) != 501 for v in tables[6]['response_rows']):
            raise ValueError('Unsupported IF response grid')

    def band(self, center):
        for low, high in self.tables[5]['descriptor_ranges_hz']:
            if low <= center < high:
                return max(1, bisect_left(self.f, low)), min(len(self.f)-1, bisect_left(self.f, high)-1)
        raise ValueError('No calibration band')

    def total_gain(self, center, register, attenuation, if_gain, lf_code=None):
        if not math.isfinite(center) or not 9000 <= center <= 6.2e9:
            raise ValueError('Frequency outside product limits')
        if lf_code is not None:
            if not isinstance(lf_code, int) or not 1 <= lf_code <= 255 or lf_code == 128:
                raise ValueError('Invalid LF gain code')
            b = self.tables[2]
            setting = f32(20*math.log10((1+(lf_code >> 7)*6.079458)*0.055744*(lf_code & 127)))
            i = next((i for i, v in enumerate(b['settings']) if abs(v-setting) <= 0.001), None)
            if i is None:
                raise ValueError('LF code lacks a measured VGA setting')
            gain = f32(b['measured_values'][i]-b['measured_values'][11])
            x, y = b['frequency_hz'], b['measured_rows'][lf_code >> 7]
            k = bisect_right(x, center)-1
            left, right = max(0, k-1), min(len(x), k+3)
            return gain+pchip(x[left:right], y[left:right], [center])[0]
        config = rf_config(register)
        left, right = self.band(center)
        k = bisect_right(self.f, center)-1
        left, right = max(left, k-1), min(right, k+2)
        path = (self.g[config][left] if right <= left else
                pchip(self.f[left:right+1], self.g[config][left:right+1], [center])[0])
        b = self.tables[4]
        relative = [[f32(v-z) for v, z in zip(row, b['measured_rows'][0])] for row in b['measured_rows']]
        atten = linear(b['settings'], [linear(b['frequency_hz'], row, center) for row in relative], attenuation)
        b = self.tables[3]
        zero = b['measured_values'][b['settings'].index(0)]
        gain = linear(b['settings'], [f32(v-zero) for v in b['measured_values']], if_gain)
        return path+atten+gain

    def scale(self, *args, **kwargs):
        return 10**(-self.total_gain(*args, **kwargs)/20)/32767

    def rf_eq(self, requested_center, center, register, first_if, high_side, frequencies):
        config = rf_config(register)
        if first_if not in (1190e6, 2440e6) or not 22e6 <= center <= 6.2e9:
            raise ValueError('Unsupported RF path')
        if frequencies != [3e6+i*100000 for i in range(501)]:
            raise ValueError('Unsupported RF EQ axis')
        offset = [v-28e6 for v in frequencies]
        # Routing follows the requested frequency, even when PLL rounding lands
        # on the next band's boundary. EQ frequencies follow the tuned LO.
        lo, hi = self.band(requested_center)
        if center < 35e6:
            # Natural spline with a linear extension of the first two points.
            x = [v-center for v in self.f]
            y = self.g[config]
            step = x[1]-x[0]
            start = x[0]
            while start > -25e6:
                start -= step
            start -= step
            count = round((x[0]-start)/step)
            extra = [start+i*step for i in range(count)]
            extra_y = [y[0]+(y[1]-y[0])*(v-x[0])/step for v in extra]
            response = spline(extra+x, extra_y+y, offset)
        elif center-25e6 < self.f[lo] or center+25e6 > self.f[hi]:
            # The EQ estimator excludes the final stored point at the upper
            # device edge. It uses a five-point linear fit when the entire
            # 50 MHz correction grid cannot fit inside the current RF band.
            if hi == len(self.f)-1:
                hi -= 1
            near = min(range(lo, hi+1), key=lambda i: abs(self.f[i]-center))
            left = max(lo, near-2) if center-25e6 < self.f[lo] else min(hi, near+2)-4
            if left < lo or left+4 > hi:
                raise ValueError('Insufficient band-local fit points')
            x, y = self.f[left:left+5], self.g[config][left:left+5]
            mx, my = sum(x)/5, sum(y)/5
            slope = sum((a-mx)*(b-my) for a, b in zip(x, y))/sum((a-mx)**2 for a in x)
            response = [slope*v for v in offset]
        else:
            response = pchip(self.f[lo:hi+1], self.g[config][lo:hi+1], [center+v for v in offset])
        a = self.tables[6]['response_rows'][1 if first_if == 2440e6 else 0]
        if not high_side:
            a = a[::-1]
        return [-(v-a[250]+r-response[250]) for v, r in zip(a, response)]

    def lf_eq(self, center, code, frequencies):
        b = self.tables[2]
        if code not in range(1, 256) or code == 128 or not 9000 <= center < 22e6:
            raise ValueError('Unsupported LF path')
        f, g = b['frequency_hz'], b['measured_rows'][code >> 7]
        if len(frequencies) != 401 or frequencies[0] != -45e6 or frequencies[-1] != 45e6:
            raise ValueError('Unsupported LF EQ axis')
        near_x = [-v for v in f[:3][::-1]]+[0]+f[:3]
        near_y = g[:3][::-1]+[g[0]-0.5]+g[:3]
        values = pchip(f, g, [max(abs(v), f[0]) for v in frequencies])
        for i, v in enumerate(frequencies):
            if abs(v) <= 15000:
                values[i] = spline(near_x, near_y, [v])[0]
        reference = spline(frequencies, values, [center])[0]
        return [reference-v for v in values]

export const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
export const finiteOr = (value, fallback) => Number.isFinite(value) ? value : fallback;
export function validateRequest(request, controlIds) {
  for (const key of ['startHz', 'stopHz', 'pointCount', 'rbwHz']) {
    if (Object.hasOwn(request, key) && !(key === 'rbwHz' && request[key] == null) && !Number.isFinite(request[key])) {
      throw new Error(`${key} must be a finite number.`);
    }
  }
  for (const key of controlIds) {
    if (Object.hasOwn(request.controls ?? {}, key) && request.controls[key] !== null && !Number.isFinite(request.controls[key])) {
      throw new Error(`${key} must be a finite number.`);
    }
  }
}
export function normalizeRange(request, prior, min, max, minSpan) {
  let startHz = clamp(finiteOr(request.startHz, prior.startHz), min, max);
  let stopHz = clamp(finiteOr(request.stopHz, prior.stopHz), min, max);
  if (stopHz - startHz < minSpan) {
    stopHz = Math.min(max, startHz + minSpan);
    startHz = Math.max(min, stopHz - minSpan);
  }
  return { startHz, stopHz };
}
export function selectedSerial(device) {
  const fromId = device.id?.startsWith('usb:rsa306b:') ? device.id.slice('usb:rsa306b:'.length) : undefined;
  const explicit = typeof device.config?.serial === 'string' ? device.config.serial.trim() : '';
  if (explicit && fromId && explicit !== fromId) throw new Error('Device serial does not match the selected analyzer ID.');
  const serial = explicit || fromId;
  if (!serial || serial.length > 100 || /[\x00-\x1f]/.test(serial)) throw new Error('Select an RSA306B or enter its serial number in device settings.');
  return serial;
}
export function deviceDescriptor(serial) {
  return { id: `usb:rsa306b:${serial}`, name: `Tektronix RSA306B ${serial}`, product: 'plugin:rsa306b/rsa306b',
    config: { serial }, transport: { kind: 'usb', path: `rsa306b:${serial}` } };
}

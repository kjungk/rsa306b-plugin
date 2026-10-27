import { SoundBasePlugin, runPlugin } from '@soundbase/plugin-shell';
import * as adapter from './adapter.js';

// Connect the SoundBase shell to the device adapter and worker lifecycle.
class Plugin extends SoundBasePlugin {
  // Prerequisite diagnostics and worker cleanup are plugin-wide.
  async init(config) { adapter.init?.(this, config); }
  async configUpdated(config) { adapter.configUpdated?.(this, config); }
  async destroy() { await adapter.destroy?.(); }
  async discoverDevices() {
    return adapter.discoverDevices?.(this.config) ?? [];
  }
}

if (typeof adapter.createSpectrumAnalyzerAdapter === 'function') {
  Plugin.prototype.createSpectrumAnalyzerAdapter = function (device) {
    return adapter.createSpectrumAnalyzerAdapter(device, this.config);
  };
}

if (typeof adapter.createMonitoringAdapter === 'function') {
  Plugin.prototype.createMonitoringAdapter = function (device) {
    return adapter.createMonitoringAdapter(device, this.config);
  };
}

export default runPlugin(Plugin, {
  manifestPath: new URL('./soundbase-plugin.json', import.meta.url),
});

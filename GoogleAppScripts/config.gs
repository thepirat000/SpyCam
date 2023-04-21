const props = PropertiesService.getScriptProperties();

function setConfig(device, setting, value) {
  const key = device + ":" + setting;
  props.setProperty(key, value);
}

function getConfig(device, setting, defaultValue) {
  const key = device + ":" + setting;
  return props.getProperty(key) ?? defaultValue;
}
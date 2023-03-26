const props = PropertiesService.getScriptProperties();

function doGet(e) {
  const device = e?.parameter?.device ?? "unknown";
  const date = new Date();
  
  let unixDate = Math.floor(date.getTime()/1000);
  // fix for the timezone GMT-6
  unixDate += 3600 * -6;

  let count = parseInt(props.getProperty('execution-count') ?? 0) + 1;
  props.setProperty('execution-count', count);

  // Cycle minimum time in seconds
  const minCycleSeconds = 60;

  // Period in number of cycles
  const period_gs = 1;
  const period_sd = 2;
  const period_conf = 5;

  const flash = date.getHours() > 7 && date.getHours() < 19 ? 0 : 1;
  const v_flip = 1;
  const frame_size = 13;
  const quality = 15;  // 10-63
    
  const configMessage = `${unixDate}:${minCycleSeconds},${period_gs},${period_sd},${period_conf},${flash},${frame_size},${v_flip},${quality}`;
  console.log(configMessage);

  return ContentService.createTextOutput(configMessage);
}


function setConfig(device, setting, value) {
  const key = device + ":" + setting;
  props.setProperty(key, value);
}

function getConfig(device, setting, defaultValue) {
  const key = device + ":" + setting;
  return props.getProperty(key) ?? defaultValue;
}


// frame_size table:
// 0- 96x96
// 1- 160x120
// 2- 176x144
// 3- 240x176
// 4- 240x240
// 5- 320x240
// 6- 400x296
// 7- 480x320
// 8- 640x480
// 9- 800x600
// 10- 1024x768
// 11- 1280x720
// 12- 1280x1024
// 13- 1600x1200

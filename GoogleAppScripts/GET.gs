// Device configuration GET endpoint
function doGet(e) {
  const device = e?.parameter?.device ?? "unknown";
  console.log(device + " device");

  const date = new Date();

  // fix for the timezone GMT-6
  let unixDate = Math.floor(date.getTime()/1000);
  unixDate += 3600 * -6;

  // Update last-seen variable
  setConfig(device, 'last-seen', Utilities.formatDate(date, "GMT-6", "yyyy-MM-dd HH:mm:ss"))

  // Update get count variable
  let count = parseInt(getConfig(device, 'get-count') ?? 0) + 1;
  setConfig(device, 'get-count', count);

  /******** <CONFIG> ********/

  // Main Cycle minimum time in seconds
  const minCycleSeconds = 60;

  // Period in number of cycles (no-motion) (0 to turn off)
  const period_gs = 5; // 5;
  const period_sd = 10; // 10;
  const period_conf = 15;
  const period_restart = 720;

  // Motion cetection enabled
  const motionDetectionEnabled = 1;  // 1
  // Motion detection cycle minimum time in seconds
  const minMotionSeconds = 5;
  
  // Camera config
  const flash = date.getHours() > 6 && date.getHours() < 22 ? 0 : 1;
  const v_flip = 0;
  const brightness = 0; // -2 to 2
  const saturation = 0; // -2 to 2
  const frame_size = 13;
  const quality = 14;  // 10-63

  /******** </CONFIG> ********/

  const configMessage = `${unixDate}:${minCycleSeconds},${period_gs},${period_sd},${period_conf},${period_restart},${flash},${frame_size},${v_flip},${brightness},${saturation},${quality},${motionDetectionEnabled},${minMotionSeconds}`;
  console.log(configMessage);

  // Update last-config variable
  setConfig(device, 'last-config', configMessage);

  return ContentService.createTextOutput(configMessage);
}

// frame_size table:
// 0- 96x96     1- 160x120    2- 176x144      3- 240x176      4- 240x240    5- 320x240      6- 400x296    
// 7- 480x320      8- 640x480      9- 800x600 10- 1024x768    11- 1280x720  12- 1280x1024   13- 1600x1200

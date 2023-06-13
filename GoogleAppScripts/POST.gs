/*
  HTTP POST (Parameters can be provided application/json body, or urlencoded)
  Parameters:
    - device: device name
    - image: base64 encoded image
    - telegram: 1/0 to indicate if the image should be sent to telegram
    - su: seconds up
    - ws: wifi signal dBm
    - cc: cycle counter
    - cm: counter_pics_motion
    - cg: counter_pics_gs
    - cs: counter_pics_sd
    - bf: heap free bytes
    - us: used megabytes in the SD card
*/
function doPost(e) {
  const postData = e?.postData?.type?.indexOf("application/json") >= 0 ? JSON.parse(e.postData.contents) : null;
  const parameter = e?.parameter;

  const device = parameter?.device ?? postData?.device ?? "unknown";
  const data = parameter?.image ?? postData?.image ?? "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAgAAAAIAQMAAAD+wSzIAAAABlBMVEX///+/v7+jQ3Y5AAAADklEQVQI12P4AIX8EAgALgAD/aNpbtEAAAAASUVORK5CYII";
  const telegram = parameter?.telegram ? parseInt(parameter?.telegram) : postData?.telegram ? parseInt(postData?.telegram) : 1;

  const deviceStatus = getDeviceStatus(device, e);

  console.log(deviceStatus);

  // Update last-device-status variable
  setConfig(device, 'last-device-status', deviceStatus)

  // Update last-seen variable
  setConfig(device, 'last-seen', Utilities.formatDate(new Date(), "GMT-6", "yyyy-MM-dd HH:mm:ss"))

  // Update post count variable
  let count = parseInt(getConfig(device, 'post-count') ?? 0) + 1;
  setConfig(device, 'post-count', count);

  // Upload image to google drive
  const filename = getFilename(telegram);
  const file = uploadImageGoogleDrive(device, filename, data);
  const fileDirectLink = "https://drive.google.com/uc?export=view&id=" + file.getId();

  // Push telegram notification
  if (telegram) {
    notifyImageTelegram(device, filename, fileDirectLink, deviceStatus);
  }

  return ContentService.createTextOutput(fileDirectLink);
}

function getDeviceStatus(device, e) {
  const parameter = e?.parameter;
  const seconds_up = parameter?.su;
  const wifi_signal_dbm = parameter?.ws ? parseInt(parameter?.ws) : -90;
  const cycle_counter = parameter?.cc;
  const counter_pics_motion = parameter?.cm;
  const counter_pics_gs = parameter?.cg;
  const counter_pics_sd = parameter?.cs;
  const heap_free_bytes = parameter?.bf;
  const temp_celsius = parameter?.tc ? parseInt(parameter?.tc) : 0;
  const sd_card_used_mb = parameter?.us ? parseInt(parameter?.us) : 0;

  const minutes_up = seconds_up ? parseInt(seconds_up) : 0;
  const signal_info_text = wifi_signal_dbm <= -80 ? "Bad" : wifi_signal_dbm <= -75 ? "Weak" : wifi_signal_dbm <= -70 ? "Normal" : wifi_signal_dbm <= -65 ? "Good" : wifi_signal_dbm <= -60 ? "Very Good" : "Excellent";

  const lastConfig = getConfig(device, 'last-config', 'N/A').split(':')[1];

  let info = "Up " + minutes_up + " mins. WiFi " + wifi_signal_dbm + " dBm (" + signal_info_text + ").\nCycles: " + cycle_counter + 
    ". Temp: " + temp_celsius + "ºC. " + heap_free_bytes + " bytes free.\nMO: " + counter_pics_motion + ", GS: " + counter_pics_gs + ", SD: " + counter_pics_sd + ", Used: " + sd_card_used_mb + "MB\nConfig: " + lastConfig;
  
  return info;
}

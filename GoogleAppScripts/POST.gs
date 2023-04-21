/*
  HTTP POST (Parameters can be provided application/json body, or urlencoded)
  Parameters:
    - hook: if 1, then no other parameters are passed and the body is from the telegram webhook
    otherwise:
    - device: device name
    - image: base64 encoded image
    - telegram: 1/0 to indicate if the image should be sent to telegram
*/
function doPost(e) {
  if (e?.parameter?.hook) {
    hookHandler(e);
  } else {
    const postData = e?.postData?.type?.indexOf("application/json") >= 0 ? JSON.parse(e.postData.contents) : null;
    const parameter = e?.parameter;

    const device = parameter?.device ?? postData?.device ?? "unknown";
    const data = parameter?.image ?? postData?.image ?? "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAgAAAAIAQMAAAD+wSzIAAAABlBMVEX///+/v7+jQ3Y5AAAADklEQVQI12P4AIX8EAgALgAD/aNpbtEAAAAASUVORK5CYII";
    const telegram = parameter?.telegram ? parseInt(parameter?.telegram) : postData?.telegram ? parseInt(postData?.telegram) : 1;

    console.log("Device: " + device + " Telegram: " + telegram);

    // Upload image to google drive
    const filename = getFilename(telegram);
    const file = uploadImageGoogleDrive(device, filename, data);
    const fileDirectLink = "https://drive.google.com/uc?export=view&id=" + file.getId();

    // Push telegram notification
    if (telegram) {
      notifyImageTelegram(device, filename, fileDirectLink);
    }

    return ContentService.createTextOutput(fileDirectLink);
  }
}

function hookHandler(e) {
  const data = JSON.parse(e.postData?.contents);
  const last_update_processed = props.getProperty("last_update_id") ?? 0;
  if (last_update_processed >= data.update_id) {
    // discard previous notifications
    return;
  }
  const user = data.update_id + ": " + xxx.message.from.first_name + " " + xxx.message.from.last_name;
  sendMessageTelegram("ECHO " + user);
}



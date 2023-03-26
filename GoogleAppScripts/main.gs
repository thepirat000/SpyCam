/*
  HTTP POST (Parameters can be provided application/json body, or urlencoded)
  Parameters:
    - device: device name
    - image: base64 encoded image
*/
function doPost(e) {
  const postData = e?.postData?.type?.indexOf("application/json") >= 0 ? JSON.parse(e.postData.contents) : null;
  const parameter = e?.parameter;

  const device = parameter?.device ?? postData?.device ?? "unknown";
  const data = parameter?.image ?? postData?.image ?? "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAgAAAAIAQMAAAD+wSzIAAAABlBMVEX///+/v7+jQ3Y5AAAADklEQVQI12P4AIX8EAgALgAD/aNpbtEAAAAASUVORK5CYII";

  console.log("Device: " + device);

   // Ensure folder exists
  const folder = ensureDeviceFolder(device);
  
  // Upload image to drive
  const file = uploadImage(device, folder, data);

  const response = {
    device: device,
    url: "https://drive.google.com/uc?authuser=0&id=" + file.getId(),
    alt_url: file.getUrl()
  }

  return ContentService.createTextOutput(JSON.stringify(response)).setMimeType(ContentService.MimeType.JSON);
}

function ensureDeviceFolder(device) {
  const rootFolderName = "ESP32-CAM";
  
  let folders = DriveApp.getFoldersByName(rootFolderName);
  const rootFolder = folders.hasNext() ? folders.next() : DriveApp.createFolder(rootFolderName);

  folders = rootFolder.getFoldersByName(device);
  const deviceFolder = folders.hasNext() ? folders.next() : rootFolder.createFolder(device);

  const day = Utilities.formatDate(new Date(), "GMT-6", "yyyy-MM-dd");

  folders = deviceFolder.getFoldersByName(day);
  const dateFolder = folders.hasNext() ? folders.next() : deviceFolder.createFolder(day);

  console.log("Folder: " + dateFolder.getName());

  return dateFolder;
}

function uploadImage(device, folder, imageSource) {
  const data = imageSource.substring(imageSource.indexOf(",") + 1);
  const blobData = Utilities.base64Decode(data);
  
  const now = Utilities.formatDate(new Date(), "GMT-6", "yyyy-MM-dd HH.mm.ss");
  const filename = "Capture " + now + ".jpg";
  const blob = Utilities.newBlob(blobData, "image/png", filename);

  console.log("File: " + filename);

  const file = folder.createFile(blob);
  file.setDescription("Uploaded by " + device + " @ " + now.replace(/\./g, ":"));

  return file;
}

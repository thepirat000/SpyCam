const cache = CacheService.getScriptCache();

function uploadImageGoogleDrive(device, filename, imageSource) {
  const folder = ensureDeviceFolder(device);
  const data = imageSource.substring(imageSource.indexOf(",") + 1);
  const blobData = Utilities.base64Decode(data);
  
  const blob = Utilities.newBlob(blobData, "image/png", filename);

  console.log("File: " + filename);

  const file = folder.createFile(blob);
  file.setDescription("Uploaded by " + device);

  return file;
}

function getFilename(isMotion) {
  const now = Utilities.formatDate(new Date(), "GMT-6", "yyyy-MM-dd HH.mm.ss");
  return (isMotion ? "Motion " : "Capture ") + now + ".jpg";
}

function ensureDeviceFolder(device) {
  const rootFolderName = "ESP32-CAM";
  const day = Utilities.formatDate(new Date(), "GMT-6", "yyyy-MM-dd");

  let folderExistsCacheKey = "folder-exists-" + device + "-" + day;
  let folderId = cache.get(folderExistsCacheKey);

  if (folderId != null) {
    console.log("Folder exists (cache hit)");
    return DriveApp.getFolderById(folderId);
  }

  let folders = DriveApp.getFoldersByName(rootFolderName);
  const rootFolder = folders.hasNext() ? folders.next() : DriveApp.createFolder(rootFolderName);

  folders = rootFolder.getFoldersByName(device);
  const deviceFolder = folders.hasNext() ? folders.next() : rootFolder.createFolder(device);

  folders = deviceFolder.getFoldersByName(day);
  const dateFolder = folders.hasNext() ? folders.next() : deviceFolder.createFolder(day);

  folderId = dateFolder.getId();
  console.log("Folder: " + dateFolder.getName() + " (" + folderId + ")");
 
  cache.put(folderExistsCacheKey, folderId, 86400); // cache for 1 day
  
  return dateFolder;
}
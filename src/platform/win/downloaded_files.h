#ifndef BROUK_PLATFORM_WIN_DOWNLOADED_FILES_H_
#define BROUK_PLATFORM_WIN_DOWNLOADED_FILES_H_

#include <string>

namespace brouk::win {

// File actions on a finished download (014), shared by the downloads page
// and the list under the toolbar button. The paths are the downloads' own,
// as the service recorded them. UI thread.
bool DownloadedFileExists(const std::string& path);
void OpenDownloadedFile(const std::string& path);
// File Explorer with the file selected ("Show in folder").
void RevealDownloadedFile(const std::string& path);
// Moves the file to the Recycle Bin (Chrome's "Delete" on Windows).
bool RecycleDownloadedFile(const std::string& path);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_DOWNLOADED_FILES_H_

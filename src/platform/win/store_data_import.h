#ifndef BROUK_PLATFORM_WIN_STORE_DATA_IMPORT_H_
#define BROUK_PLATFORM_WIN_STORE_DATA_IMPORT_H_

#include <string>

#include "core/data_folder_import.h"

namespace brouk::win {

// The Store app's import of the direct download's data (032). The two
// copies keep separate data; the Store app asks once (and Settings offers
// it later) whether to move or copy the direct download's data in. As the
// import replaces the data Brouk has open, Brouk writes a request, quits
// and starts again, and the new start imports before it opens anything.

// The name of the mutex a running Brouk holds (also waited for by the
// installer, W4): the Store app has its own, so the two copies can tell
// each other apart.
std::wstring RunningMutexName();

// Whether the Store app can import: it is the Store app and the direct
// download's data folder exists. Any thread.
bool CanImportDirectDownloadData();

// Whether the first start's question was answered (or an import was
// done), so it is not asked again; and remembering that. Any thread.
bool DirectDownloadImportOffered();
void SetDirectDownloadImportOffered();

// Writes the request for the next start; false if it could not be
// written. Any thread.
bool RequestDirectDownloadImport(DataImportMode mode);

// At the start of the Store app's browser process, before anything opens
// the data folder: runs a requested import, asking to quit the direct
// download first while it runs, with a small window while it copies, and
// a message if it failed. Main thread, before CefInitialize.
void RunRequestedDirectDownloadImport();

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_STORE_DATA_IMPORT_H_

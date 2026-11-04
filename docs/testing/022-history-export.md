# 022 – History in profile export: manual test checklist

Run on macOS and on Windows.

1. Profiles → Manage Profiles → **Export…** on a profile with some history. Expected: a form under the profile with "Include history" unticked and the two limits greyed out.
2. Leave it unticked and export. Expected: as before (export password if it has passwords, then the save dialog); the dialog's note does not mention history; the file has no `history:` section.
3. Export again with **Include history** and "The last 7 days". Expected: the note mentions history; the file's `history:` lists only pages visited in the last 7 days, newest first.
4. Export with "The last 3 visited pages". Expected: 3 pages, each with all its visits.
5. Type `0` or `abc` as the number and click Export…. Expected: "Enter a number of days from 1 to 3650." and nothing is exported.
6. Import the file from step 3 (Import…). Expected: a new profile; open it and type part of a page's address — it is suggested; the history page lists the visits under the new profile, with typed visits counted.
7. Set Settings → History → "Keep history for" to 1 week, then import the file from step 4 if it has older visits. Expected: visits older than a week are not imported.
8. Import the file from step 2. Expected: imported as before, no history.

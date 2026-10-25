/** The downloads page (014) as pure functions, tested without a DOM. */
import type { Download, DownloadAction } from './api.js';

/** Downloads whose file name or URL contains every word (case ignored). */
export function searchDownloads(downloads: readonly Download[], query: string): Download[] {
  const words = query.toLowerCase().split(/\s+/).filter((w) => w !== '');
  return downloads.filter((d) => {
    const hay = `${d.name} ${d.url}`.toLowerCase();
    return words.every((w) => hay.includes(w));
  });
}

/** "Today", "Yesterday" or the date, for grouping the list by day. */
export function dayLabel(start: number, now: Date): string {
  const day = new Date(start * 1000);
  const same = (a: Date, b: Date): boolean =>
    a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
  if (same(day, now)) return 'Today';
  const yesterday = new Date(now);
  yesterday.setDate(now.getDate() - 1);
  if (same(day, yesterday)) return 'Yesterday';
  return day.toLocaleDateString(undefined, { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' });
}

/** What the row shows as status: "Deleted" for a finished file that is gone. */
export function statusOf(download: Download): string {
  return download.state === 'complete' && !download.exists ? 'Deleted' : download.status;
}

/** The actions that suit a download's state (014). */
export function actionsFor(download: Download): DownloadAction[] {
  switch (download.state) {
    case 'in_progress':
      return ['pause', 'cancel', 'copy', 'remove'];
    case 'paused':
      return ['resume', 'cancel', 'copy', 'remove'];
    case 'failed':
    case 'cancelled':
      return ['retry', 'copy', 'remove'];
    case 'complete':
      // A deleted or moved file: only the address (copy, download again).
      return download.exists ? ['open', 'reveal', 'copy', 'remove', 'delete'] : ['retry', 'copy', 'remove'];
  }
}

export const kActionLabels: Readonly<Record<DownloadAction, string>> = {
  open: 'Open',
  reveal: 'Show in Finder',
  copy: 'Copy link',
  pause: 'Pause',
  resume: 'Resume',
  cancel: 'Cancel',
  retry: 'Download again',
  remove: 'Remove from list',
  delete: 'Delete file',
};

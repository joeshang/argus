const encoder = new TextEncoder();
const PART_BYTES = 512 * 1024 - 4096; // Leave room for the native identity/envelope.
const SNAPSHOT_BYTES = 3 * 1024 * 1024;

export function snapshotMessages(metadata, frames) {
  if (frames.length > 10000) throw new Error('标签页映射超过 10000 个 frame 的采集上限');
  const parts = [[]];
  let bytes = 0;
  let total = 0;
  for (const frame of frames) {
    if (encoder.encode(frame.url).length > 65536) throw new Error('网页 URL 超过 64 KiB 的采集上限');
    const size = encoder.encode(JSON.stringify(frame)).length + 1;
    if (size > PART_BYTES || (total += size) > SNAPSHOT_BYTES) {
      throw new Error('标签页映射超过单次采集上限（3 MiB），请减少标签页或过长的 URL');
    }
    if (bytes + size > PART_BYTES) { parts.push([]); bytes = 0; }
    parts.at(-1).push(frame);
    bytes += size;
  }
  return parts.map((records, partIndex) => ({ type: 'snapshot', protocolVersion: 2,
    ...metadata, partIndex, partCount: parts.length, frames: records }));
}

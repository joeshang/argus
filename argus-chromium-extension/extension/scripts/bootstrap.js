const status = document.getElementById('status');
const errorText = document.getElementById('error');
const toggle = document.getElementById('toggle');
let enabled = false;

async function refresh(message = { type: 'argus:status' }) {
  try {
    const state = await chrome.runtime.sendMessage(message);
    if (!state || typeof state.enabled !== 'boolean') throw new Error(state?.error || '后台未返回状态');
    enabled = state.enabled;
    status.textContent = !enabled ? '采集已暂停' : state.connected ? '正在采集 · 本地服务已连接' : '等待本地服务连接（自动重试）';
    errorText.textContent = enabled && state.error ? state.error : '';
    toggle.textContent = enabled ? '暂停采集' : '恢复采集';
    toggle.disabled = false;
  } catch (error) {
    errorText.textContent = error.message;
  }
}
toggle.addEventListener('click', async () => {
  toggle.disabled = true;
  await refresh({ type: 'argus:set-enabled', enabled: !enabled });
});
void refresh();
setInterval(() => { if (!toggle.disabled) void refresh(); }, 1000);

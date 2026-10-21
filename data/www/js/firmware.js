import { setMsg, apiJson } from './util.js';

function $(id) { return document.getElementById(id); }

function paintStatus(d) {
  const st = $('fw-dfu-status');
  const note = $('fw-install-note');
  const install = $('fw-install');
  const file = $('fw-file');
  const on = !!d.dfu;
  const armed = !!d.armed;
  if (st) {
    if (!on) st.textContent = 'Status: DFU OFF (default) · USB DFU not armed';
    else if (armed) st.textContent = 'Status: DFU ON · USB DFU armed this boot';
    else st.textContent = 'Status: DFU ON · reboot pending so USB can arm';
  }
  if (note) {
    note.textContent = on
      ? 'DFU switch on. Web install still later — PlatformIO / serial still works.'
      : 'Coming soon — use PlatformIO / serial flash for now.';
  }
  const ready = !!(d.upload && armed && on);
  if (install) install.disabled = !ready;
  if (file) file.disabled = !ready;
}

export async function refreshFirmwarePanel() {
  try {
    const d = await apiJson('/api/firmware');
    const sw = $('fw-dfu');
    if (sw) sw.checked = !!d.dfu;
    paintStatus(d);
    setMsg(!d.dfu ? 'DFU off' : (d.armed ? 'DFU on and armed' : 'DFU on — reboot to arm'), 'ok');
  } catch (e) {
    setMsg(String(e.message || e), 'err');
  }
}

export function enterFirmwarePanel() {
  refreshFirmwarePanel();
}

export function bindFirmwarePanel() {
  const sw = $('fw-dfu');
  if (sw && !sw.dataset.bound) {
    sw.dataset.bound = '1';
    sw.addEventListener('change', async () => {
      try {
        setMsg(sw.checked ? 'Saving DFU on…' : 'Saving DFU off…');
        const r = await fetch('/api/firmware/dfu', {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain' },
          body: sw.checked ? 'on' : 'off'
        });
        const t = await r.text();
        let d = {};
        try { d = JSON.parse(t); } catch (_) { d = { error: t }; }
        if (!r.ok) throw new Error(d.error || t || ('HTTP ' + r.status));
        paintStatus(d);
        if (d.reboot) {
          setMsg('Saved — device rebooting to apply DFU…', 'ok');
        } else {
          setMsg(sw.checked ? 'DFU on' : 'DFU off', 'ok');
        }
      } catch (e) {
        sw.checked = !sw.checked;
        setMsg(String(e.message || e), 'err');
      }
    });
  }
  const install = $('fw-install');
  if (install && !install.dataset.bound) {
    install.dataset.bound = '1';
    install.addEventListener('click', () => {
      setMsg('Firmware install not implemented yet', 'warn');
    });
  }
}

"use strict";
(() => {
  const el = id => document.getElementById(id);
  const selector = el("target-select"), confirm = el("confirm-target");
  const installer = el("installer"), status = el("install-status");
  const targets = {
    "heltec-v4-r8-tft-high-band": {flash: 16777216, psram: 8388608, version: "0.5.0", channel: "stable"},
    "cardputer-adv-lora1262": {flash: 8388608, psram: 0, version: "0.5.0", channel: "stable"}
  };
  let catalog, ready = false, selection = 0, reason = "Checking release metadata…";
  const localAcceptance = () => ["localhost", "127.0.0.1", "[::1]"].includes(location.hostname) &&
    new URLSearchParams(location.search).get("acceptance") === "1" && catalog?.local_preview === true;
  const safeName = name => typeof name === "string" && /^[A-Za-z0-9_.-]+$/.test(name);
  const hash = value => typeof value === "string" && /^[0-9a-f]{64}$/.test(value);
  function refresh() {
    installer.hidden = !(ready && confirm.checked);
    status.textContent = !ready ? reason : confirm.checked
      ? "Ready. Your browser will ask you to choose a USB port."
      : "Confirm the selected hardware and recovery instructions to enable installation.";
  }
  async function choose() {
    const current = ++selection;
    ready = false;
    confirm.checked = false;
    reason = "Checking selected firmware…";
    el("download-factory").hidden = true;
    el("download-app").hidden = true;
    refresh();
    try {
      const r = catalog.targets.find(t => t.target === selector.value);
      const board = targets[r?.target];
      if (!board || r.version !== board.version || r.channel !== board.channel ||
          r.flash_bytes !== board.flash || r.psram_bytes !== board.psram ||
          !hash(r.factory_sha256) || !safeName(r.factory_file) ||
          !safeName(r.source_file) || !safeName(r.sd_file) ||
          r.manifest !== "firmware/" + r.target + "/manifest.json" ||
          r.guide !== "targets/" + r.target + "/guides/" + (r.target === "cardputer-adv-lora1262" ? "CARDPUTER_ADV" : "USER_GUIDE") + ".html") {
        throw new Error("Release metadata mismatch");
      }
      if (r.target === "cardputer-adv-lora1262" && (!safeName(r.app_file) || !hash(r.app_sha256))) throw new Error("Launcher metadata mismatch");
      const response = await fetch(r.manifest, {cache: "no-store"});
      if (!response.ok) throw new Error("Manifest unavailable");
      const manifest = await response.json();
      if (current !== selection) return;
      const build = manifest.builds?.[0];
      if (manifest.version !== r.version || manifest.builds.length !== 1 ||
          build.chipFamily !== "ESP32-S3" || build.parts.length !== 1 ||
          build.parts[0].offset !== 0 || build.parts[0].path !== "../../downloads/" + r.factory_file) throw new Error("Manifest mismatch");
      el("target-name").textContent = r.name;
      el("target-version").textContent = r.version;
      el("target-memory").textContent = r.flash_bytes / 1048576 + " MB flash · " + r.psram_bytes / 1048576 + " MB PSRAM";
      el("confirm-label").textContent = "I checked my " + r.name + " hardware and read its recovery instructions.";
      el("target-guide").href = r.guide;
      el("download-factory").href = "downloads/" + r.factory_file;
      el("download-source").href = "downloads/" + r.source_file;
      el("download-sd").href = "downloads/" + r.sd_file;
      el("factory-sha").textContent = r.factory_sha256;
      const relink = new URL(r.relink_url);
      if (relink.origin !== "https://github.com" || !relink.pathname.startsWith("/valleytechsolutions/LORA-Tunnel/releases/download/") || relink.search || relink.hash) throw new Error("Dependency source URL mismatch");
      el("download-relink").href = r.relink_url;
      const permitted = r.public_enabled === true || localAcceptance();
      el("target-state").textContent = r.public_enabled ? "Release " + r.version + " · see validation and known limitations" : "Hardware acceptance pending · local test preparation";
      if (!permitted) {
        reason = "Installation is awaiting physical hardware acceptance or release authorization for this target.";
        refresh();
        return;
      }
      el("download-factory").hidden = false;
      if (r.app_file) {
        el("download-app").href = "downloads/" + r.app_file;
        el("download-app").hidden = false;
      }
      el("install-button").setAttribute("manifest", r.manifest);
      ready = true;
      refresh();
    } catch (_) {
      if (current !== selection) return;
      reason = "Installation is unavailable because release metadata could not be verified. Reload, or use the checksummed manual download and guide.";
      refresh();
    }
  }
  confirm.checked = false;
  confirm.addEventListener("change", refresh);
  selector.addEventListener("change", choose);
  fetch("catalog.json", {cache: "no-store"}).then(async response => {
    if (!response.ok) throw new Error("Catalog unavailable");
    catalog = await response.json();
    if (catalog.schema !== 2 || !Array.isArray(catalog.targets) || catalog.targets.length !== 2 ||
        new Set(catalog.targets.map(t => t.target)).size !== 2 ||
        catalog.targets.some(t => !targets[t.target])) throw new Error("Catalog mismatch");
    await choose();
  }).catch(() => {
    reason = "Installation is unavailable because release metadata could not be verified.";
    refresh();
  });
})();

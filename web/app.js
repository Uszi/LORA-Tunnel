"use strict";
(() => {
  const confirm = document.getElementById("confirm-target");
  const installer = document.getElementById("installer");
  const status = document.getElementById("install-status");
  let ready = false;
  let reason = "Checking release metadata…";
  function refresh() {
    installer.hidden = !(ready && confirm.checked);
    status.textContent = !ready ? reason : confirm.checked
      ? "Ready. Your browser will ask you to choose a USB port."
      : "Confirm the hardware and backup instructions to enable installation.";
  }
  confirm.checked = false;
  confirm.addEventListener("change", refresh);
  Promise.all([fetch("release.json", {cache:"no-store"}), fetch("firmware/manifest.json", {cache:"no-store"})])
    .then(async responses => {
      if (responses.some(response => !response.ok)) throw new Error("Release metadata unavailable");
      const [release, manifest] = await Promise.all(responses.map(response => response.json()));
      const build = manifest.builds?.[0];
      if (release.channel !== "beta" || release.version !== manifest.version ||
          release.target !== "heltec-v4-r8-tft-us915" || release.flash_bytes !== 16777216 ||
          release.psram_bytes !== 8388608 || !/^[0-9a-f]{64}$/.test(release.factory_sha256) ||
          manifest.builds.length !== 1 || build.chipFamily !== "ESP32-S3" ||
          build.parts.length !== 1 || build.parts[0].offset !== 0 ||
          build.parts[0].path !== "../downloads/" + release.factory_file) {
        throw new Error("Release metadata mismatch");
      }
      ready = true;
      refresh();
    })
    .catch(() => {
      reason = "Installation is unavailable because release metadata could not be verified. Reload, or use the checksummed manual download and guide.";
      refresh();
    });
})();

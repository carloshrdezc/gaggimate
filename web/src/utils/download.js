function escapeHtml(value) {
  return String(value || '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function renderDeferredDownloadPage(targetWindow, filename, url) {
  if (!targetWindow || targetWindow.closed) return;

  const safeFilename = escapeHtml(filename);
  targetWindow.document.open();
  targetWindow.document.write(`<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Download ${safeFilename}</title>
    <style>
      body {
        font-family: system-ui, sans-serif;
        padding: 24px;
        line-height: 1.5;
        background: #111827;
        color: #f9fafb;
      }
      a {
        display: inline-block;
        margin-top: 16px;
        padding: 12px 16px;
        border-radius: 10px;
        background: #2563eb;
        color: white;
        text-decoration: none;
        font-weight: 600;
      }
    </style>
  </head>
  <body>
    <h1>Preparing download</h1>
    <p>If your download does not start automatically, tap the button below.</p>
    <a id="download-link" href="${url}" download="${safeFilename}">Download ${safeFilename}</a>
    <script>
      setTimeout(function () {
        document.getElementById('download-link').click();
      }, 50);
    </script>
  </body>
</html>`);
  targetWindow.document.close();
}

function renderDeferredErrorPage(targetWindow, error) {
  if (!targetWindow || targetWindow.closed) return;

  targetWindow.document.open();
  targetWindow.document.write(`<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Export failed</title>
  </head>
  <body style="font-family: system-ui, sans-serif; padding: 24px; line-height: 1.5;">
    <h1>Export failed</h1>
    <p>${escapeHtml(error?.message || 'Unknown error')}</p>
  </body>
</html>`);
  targetWindow.document.close();
}

export function prepareDownload(filename) {
  let targetWindow = null;

  try {
    targetWindow = window.open('', '_blank');
    if (targetWindow && !targetWindow.closed) {
      targetWindow.document.write('<p style="font-family: system-ui, sans-serif; padding: 24px;">Preparing download...</p>');
      targetWindow.document.close();
    }
  } catch {
    targetWindow = null;
  }

  return {
    filename,
    targetWindow,
    fail(error) {
      renderDeferredErrorPage(targetWindow, error);
    },
  };
}

export function downloadBlob(blob, filename, options = {}) {
  const url = URL.createObjectURL(blob);
  const targetWindow = options?.targetWindow || null;

  if (targetWindow && !targetWindow.closed) {
    renderDeferredDownloadPage(targetWindow, filename, url);
    setTimeout(() => URL.revokeObjectURL(url), 60_000);
    return;
  }

  const a = document.createElement('a');
  a.style.display = 'none';
  a.href = url;
  a.download = filename;

  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function downloadJson(json, filename, options = {}) {
  const jsonStr = JSON.stringify(json, undefined, 2);
  const blob = new Blob([jsonStr], { type: 'application/json' });
  downloadBlob(blob, filename, options);
}

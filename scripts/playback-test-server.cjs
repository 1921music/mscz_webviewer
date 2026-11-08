const http = require('node:http'), fs = require('node:fs'), path = require('node:path')
const root = path.resolve(process.env.MSCZ_WEB_DIR || 'build/web')
http.createServer((req, res) => {
  const p = decodeURIComponent(new URL(req.url, 'http://127.0.0.1').pathname)
  const file = path.resolve(root, '.' + (p === '/' ? '/index.html' : p))
  if (!file.startsWith(root + path.sep)) { res.writeHead(403).end(); return }
  const type = { '.js': 'text/javascript', '.wasm': 'application/wasm', '.html': 'text/html' }[path.extname(file)] || 'application/octet-stream'
  fs.readFile(file, (err, bytes) => { if (err) res.writeHead(404).end(); else { res.setHeader('Content-Type', type); res.end(bytes) } })
}).listen(8766, '127.0.0.1')

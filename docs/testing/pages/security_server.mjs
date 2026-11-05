// Test pages for the security check (029): one page with no security
// headers and flagless cookies, one with good headers. Only Node's built-in
// modules.
//
//   node docs/testing/pages/security_server.mjs [port]   default 8794
//   then open http://127.0.0.1:8794/bad and http://127.0.0.1:8794/good
import { createServer } from 'node:http';

const port = Number(process.argv[2] ?? 8794);

const kGood = {
  'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; object-src 'none'; "
    + "base-uri 'self'; form-action 'self'; frame-ancestors 'self'",
  'Strict-Transport-Security': 'max-age=31536000; includeSubDomains',
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'strict-origin-when-cross-origin',
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Permissions-Policy': 'camera=(), geolocation=()',
};

const page = (title, body, script) => `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>${title}</title></head>
<body><h1>${title}</h1>${body}<script src="/${script}"></script></body></html>`;

// The bad page's calls: a signed-in API answer without no-store or nosniff,
// a password in a URL, and CORS that allows the caller with credentials.
const kBadScript = `
// A made-up JWT, joined here so secret scanners do not take it for a real one.
localStorage.setItem('auth', ['eyJhbGciOiJIUzI1NiJ9', 'eyJzdWIiOiIxMjMifQ', 'c2lnbmF0dXJl'].join('.'));
fetch('/api/me', { headers: { Authorization: 'Bearer test-token' } });
fetch('/api/login?user=tester&password=not-a-real-password');
fetch('/api/cors', { method: 'POST', credentials: 'include' });
`;

const kGoodScript = `fetch('/api/public');`;

const server = createServer((request, response) => {
  const url = new URL(request.url ?? '/', `http://127.0.0.1:${port}`);
  const send = (status, headers, body) => {
    response.writeHead(status, headers);
    response.end(body);
  };
  switch (url.pathname) {
    case '/bad':
      return send(200, {
        'Content-Type': 'text/html; charset=utf-8',
        'Server': 'nginx/1.25.3',
        'X-Powered-By': 'Express',
        'Set-Cookie': ['session=abc', 'theme=dark', '__Host-wrong=1; Path=/app; Secure'],
      }, page('Security check: bad page', `<form action="http://example.com/save"><input type="password" aria-label="Password"></form>
<script src="https://cdn.jsdelivr.net/npm/lodash@4.17.21/lodash.min.js"></script>`, 'bad.js'));
    case '/good':
      return send(200, {
        'Content-Type': 'text/html; charset=utf-8',
        'Server': 'nginx',
        ...kGood,
        'Set-Cookie': '__Host-session=abc; Secure; HttpOnly; SameSite=Lax; Path=/',
      }, page('Security check: good page', '<p>Nothing should be found above Low.</p>', 'good.js'));
    case '/bad.js':
      return send(200, { 'Content-Type': 'text/javascript' }, kBadScript);
    case '/good.js':
      return send(200, { 'Content-Type': 'text/javascript', ...kGood }, kGoodScript);
    case '/api/me':
      return send(200, { 'Content-Type': 'application/json', 'Cache-Control': 'private' }, '{"name":"Tester"}');
    case '/api/login':
      return send(200, { 'Content-Type': 'application/json' }, '{"ok":true}');
    case '/api/cors':
      return send(200, {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': request.headers.origin ?? '*',
        'Access-Control-Allow-Credentials': 'true',
      }, '{}');
    case '/api/public':
      return send(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...kGood }, '{}');
    default:
      return send(404, { 'Content-Type': 'text/plain' }, 'Not found');
  }
});

server.listen(port, '127.0.0.1', () => {
  console.log(`Security check test pages: http://127.0.0.1:${port}/bad and http://127.0.0.1:${port}/good`);
});

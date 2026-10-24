# 017 – VPN per profile

- **Status:** draft
- **Priority:** P2
- **Depends on:** 004, 005; works with 010

## Goal

Reach a client's internal sites over the client's VPN **from one profile only**, without connecting the whole computer. Today, connecting to a client VPN routes the whole Mac (or nothing) through it. That breaks other work, mixes clients, and often needs admin rights or the vendor's client app. The user mostly works with client infrastructure through the browser, so a tunnel that only one profile uses is enough. Other profiles, other apps and the OS stay on the normal network.

## Description

### Network route of a profile

- Each profile has a **network route**:
  - **Direct** (default) — the normal network, as today.
  - **VPN connection** — all of the profile's traffic goes through one of the user's VPN connections (below).
  - **Proxy** — the profile uses a SOCKS5 or HTTP proxy the user already runs, given as host and port (for example a tunnel started by hand, or `ssh -D`).
- The route is chosen in the profile's settings and in the profile manager.
- Only that profile's tabs, downloads, DevTools requests (006) and Brouk's own requests made for that profile use the route. Other profiles in the same window stay direct.

### VPN connections

- A **VPN connection** is a named, global definition: name, type, server or config file, username, routing and the start/stop behaviour.
- Connections are managed on a "VPN connections" page in settings: add, edit, duplicate, delete, test, connect, disconnect, show log.
- A connection can be **imported** from the file the client gives: `.ovpn` (OpenVPN) or a WireGuard `.conf`. For OpenConnect types, the user enters the server address and the protocol.
- Supported types, each run by an external tool in **user space** (no network extension, no TUN device, no admin password, the OS routing table never changes):

  | Type | Covers | Tool |
  |---|---|---|
  | WireGuard | WireGuard servers | `wireproxy` |
  | OpenConnect | Cisco AnyConnect / Secure Client, Palo Alto GlobalProtect, Fortinet SSL VPN, Pulse / Ivanti, F5, Array, ocserv | `openconnect` with `ocproxy` |
  | OpenVPN | OpenVPN servers (`.ovpn`) | `openvpn` in a container with a small SOCKS server (see Notes) |
  | SSH tunnel | Jump hosts, bastions | `ssh -D` |

- Each tool runs as a **helper process** started by Brouk. It opens a proxy on `127.0.0.1` at a port Brouk chooses, and the profile's request context is pointed at that proxy.
- Like the Bitwarden CLI (008), the tools are **installed by the user** (Homebrew, or Docker/OrbStack/Colima for OpenVPN). Brouk does not bundle them. Brouk finds them automatically, the path can be set in settings, and when a tool is missing Brouk shows how to install it.

### Connecting and disconnecting

- By default a connection starts **when the profile first needs the network** (the first tab or request of that profile). It can be set to start when Brouk starts instead.
- By default it stops when the last tab of every profile using it closes. It can also stop after a set idle time, or stay connected until Brouk quits.
- It can always be connected, disconnected and reconnected by hand: from the profile group's menu in the tab strip, from the Profiles menu, and from the VPN connections page.
- When the helper exits unexpectedly, Brouk reconnects automatically: up to 3 tries, with increasing delays, then it stops and shows the failure.
- Quitting Brouk stops all helpers; no helper keeps running after Brouk ends, also after a crash (helpers are started so that they end with Brouk).

### Status

- The profile's tab group shows the VPN state with a small icon on the group label: connecting, connected, failed, disconnected. Hover shows the connection name and, when connected, how long it has been up.
- The address bar's site info (012) says "Through VPN <name>" for tabs of a VPN profile.
- The log of the helper (its output, with secrets removed) can be shown from the status menu and the settings page, to diagnose failures.

### Never leak outside the tunnel

- **Fail closed:** while the connection is not up, the profile's requests are **not** sent directly. The tab shows a Brouk error page: "VPN <name> is not connected", with **Connect** and **Show log**. There is no fallback to the normal network.
- **DNS** of the profile goes through the tunnel (host names are resolved by the proxy, using the VPN's DNS), so internal names like `jira.client.local` work and lookups do not leak to the local DNS. Secure DNS (007) does not apply to VPN profiles.
- **WebRTC** in a VPN profile only uses the proxy (Chromium's "disable non-proxied UDP" policy), so a page cannot see or use the local network address.
- Prefetch, preconnect and other background requests of the profile use the route like every other request.

### Split routing (optional)

- By default **all** traffic of the profile goes through the tunnel.
- A connection can instead list the hosts, domains (`*.client.com`) and IP ranges (`10.0.0.0/8`) that go through it; the rest of that profile's traffic goes direct. This keeps public sites (search, docs) fast while internal ones use the VPN.

### Credentials and login

- Passwords, private keys and other secrets of a connection are stored only in the macOS Keychain (as for 008), never in YAML files or helper command lines.
- When the VPN asks for something interactive (password, one-time code, "approve on your phone", choosing a group), Brouk shows a native dialog anchored to the profile's window and passes the answer to the helper.
- A connection can be linked to a TOTP entry of the built-in password manager (008), so the one-time code is filled in automatically.
- Single sign-on logins (SAML, for example GlobalProtect or AnyConnect with Microsoft Entra ID) are opened in a Brouk window in the VPN profile, so the user signs in with the profile's existing session (see Open questions).

### With link routing (010)

- Routing rules can send a client's hosts to that client's VPN profile, so a link to `https://jira.client.local` from Outlook or Teams opens in the profile whose VPN reaches it, and the VPN connects on demand.

## Acceptance criteria

- [ ] A profile's network route can be set to Direct, a VPN connection or a proxy; Direct is the default.
- [ ] With a VPN profile connected, the OS routing table, DNS settings and network interfaces are unchanged, and no admin password is asked for.
- [ ] Sites opened in a VPN profile are reached through the tunnel (the "what is my IP" check shows the VPN's address); at the same time, sites opened in another profile, in the same window, show the normal address.
- [ ] Other apps on the Mac (for example `curl` in Terminal) are not affected by the connection.
- [ ] An internal host name that only the VPN's DNS knows opens in the VPN profile and does not resolve in other profiles.
- [ ] A WireGuard connection can be imported from a `.conf` file and connected.
- [ ] An OpenConnect connection (at least one of AnyConnect, GlobalProtect, Fortinet) can be set up and connected, including a password and a one-time code asked for in a native dialog.
- [ ] An OpenVPN connection can be imported from a `.ovpn` file and connected. (Depends on the open question about the OpenVPN backend.)
- [ ] A proxy route with a SOCKS5 or HTTP proxy given by host and port works without any helper.
- [ ] With "connect on first use", opening the first tab of a VPN profile starts the connection, and the page loads once it is up.
- [ ] While the connection is down or failing, every request of the profile fails with the "VPN is not connected" page; no request of the profile reaches the network directly (verified with a packet capture or a firewall log).
- [ ] WebRTC in a VPN profile does not reveal the local IP address (checked on a WebRTC leak test page).
- [ ] With split routing, listed hosts and IP ranges go through the tunnel and other hosts of the profile go direct.
- [ ] The tab group of a VPN profile shows its connection state, and the connection can be connected, disconnected and reconnected from the group's menu.
- [ ] When the helper process is killed, Brouk reconnects automatically, and after 3 failed tries it shows the failure with the log.
- [ ] Quitting Brouk, or Brouk crashing, leaves no helper process running.
- [ ] No VPN secret is written to disk by Brouk, shown in the log, or passed to a helper as a command-line argument.
- [ ] A missing tool is reported with install instructions, and its path can be set in settings.
- [ ] Downloads started in a VPN profile go through the tunnel.
- [ ] Profile export (005) includes the profile's network route and its connection definition without secrets; on import, the user is asked for the secrets.
- [ ] Help describes setting up each connection type. (after 016)

## Out of scope

- VPN for the whole Mac or for other apps.
- Native macOS VPN types through a network extension (IKEv2, IPsec, L2TP) and vendor client apps (Cisco Secure Client, GlobalProtect app, FortiClient).
- Bundling the VPN tools inside Brouk (may be revisited in phase 2).
- Device posture or host checks the VPN server requires beyond what the tool supports (for example Cisco's host scan or GlobalProtect's HIP report, where `openconnect` cannot answer them).
- Non-web traffic started from a page, such as an RDP or SSH client opened by a link.
- Several VPN connections in one profile at the same time.
- Windows (later, with 002) and mobile.

## Configuration

Global (`settings.yaml`):

- VPN connections: name, type, server or imported config, username, protocol (OpenConnect), routing (all traffic / listed hosts and IP ranges), start (on first use / at Brouk start), stop (when no tabs use it / after N minutes idle / at quit), TOTP entry (optional). Secrets in the Keychain. Imported config files are kept in `vpn/<connection-id>/`, readable by the user only.
- Paths of the tools (`wireproxy`, `openconnect`, `ocproxy`, `ssh`, container runtime), found automatically by default.

Per profile (`profiles/<id>/settings.yaml`):

- Network route: `direct` (default), `vpn` with a connection id, or `proxy` with type (SOCKS5/HTTP), host and port.

## Open questions

1. **Which VPN types are needed first?** Which products do the user's clients use (OpenVPN, WireGuard, AnyConnect, GlobalProtect, Fortinet, …)? This decides the order and whether all four types are needed.
2. **OpenVPN backend:** OpenVPN has no user-space proxy mode on macOS. The proposal is to run it in a container (Docker Desktop, OrbStack or Colima) together with a small SOCKS server. Is a container runtime acceptable? Alternatives: skip OpenVPN until there is a client that needs it, or a much larger own implementation (OpenVPN 3 core plus a user-space network stack).
3. **Local proxy port:** while a connection is up, its proxy listens on `127.0.0.1`, so other programs of the same user could use the tunnel too (Chromium cannot authenticate to a SOCKS5 proxy). Is that acceptable for phase 1, given a random port and a stop when unused? The stricter alternative is that Brouk puts an HTTP proxy with random credentials in front of each helper (more code, the helper's port would still be local).
4. **Single sign-on:** for SAML/SSO VPN logins, is a Brouk window in the VPN profile the wanted flow (so the profile's Microsoft session is reused), or should the system browser be used?
5. **One connection, several profiles:** may two profiles use the same connection at once (sharing one tunnel), or should each profile get its own?
6. **Priority and place in the roadmap.**

## Notes

- 2026-10-24 (proposal, from discussion with the user): the general approach is "VPN in a user-space helper, exposed as a local proxy, used by one profile's request context". This is the only way to scope a VPN to part of a browser without touching the OS network.
- Implementation hints, to be verified against the CEF headers before use (`CLAUDE.md`):
  - Proxy per profile: Chromium's `proxy` preference belongs to the profile, so it should be settable per request context with `CefRequestContext::SetPreference("proxy", …)` (mode `fixed_servers`, or `pac_script` for split routing) on the UI thread. Brouk must not pass the global `--proxy-server` switch, which would override all profiles. Check that this works for Alloy-style browsers with the Chrome bootstrap in CEF 154.
  - WebRTC: preference `webrtc.ip_handling_policy` = `disable_non_proxied_udp` on the request context.
  - HTTP proxy authentication, if used (open question 3): `CefRequestHandler::GetAuthCredentials` with `isProxy`.
  - SOCKS5 resolves host names on the proxy side in Chromium, which gives the remote DNS needed here. QUIC is not used through a SOCKS proxy, so HTTP/3 falls back to HTTP/2 in VPN profiles.
  - Requests Brouk makes itself for a profile (006's own request loader, favicons, downloads) use the profile's request context and so follow the route automatically. Requests not tied to a profile (update checks, the Bitwarden CLI, 010's neutral throwaway context for link wrappers) go direct.
- The helper supervision (start, health check by connecting to the proxy port, restart with back-off, stop, log with secrets removed) is portable core code in `src/core/`, tested on Linux with a fake helper. Starting processes goes through a platform interface.
- The tools are separate programs run by Brouk, not linked into it, so their licences (`openconnect` is LGPL-2.1, `wireproxy` ISC, `ocproxy` BSD) do not conflict with the library licence rule in `CLAUDE.md`, as with the Bitwarden CLI (008).
- Help text for this feature will be needed (rule 7 in `CLAUDE.md`).

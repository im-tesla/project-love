# Hosting the web app

The site is static — no build step, no npm, no backend. Hosting it is copying a
directory and pointing nginx at it. The only genuinely fiddly part is the
certificate, and that part is not optional:

> **Web Bluetooth requires a secure context.** `navigator.bluetooth` does not
> exist over plain `http://`, so the Połącz button will report that the browser
> has no Bluetooth support — even in Bluefy, on a page that is otherwise
> working perfectly. A self-signed certificate is usually rejected too.

Throughout, replace `milena-led.duckdns.org` with your domain and `love` with
whatever you want the nginx site called.

---

## 0. Check you can actually be reached

Do this first. If you are behind CGNAT, no amount of port forwarding will help
and steps 1–4 will waste an evening.

On the home server:

```bash
curl -s https://api.ipify.org
```

Then compare that to the WAN address your router reports in its own admin page.

- **They match** → you have a public IP. Continue.
- **They differ**, or the router shows something in `100.64.x.x`–`100.127.x.x`
  → you are behind **CGNAT**. Your ISP is sharing one address between many
  customers and inbound connections cannot reach you. Skip to
  [If you are behind CGNAT](#if-you-are-behind-cgnat).

This is worth checking properly in Poland: mobile and some cable ISPs use CGNAT
by default. Many will give you a public IP on request, sometimes for a small
monthly fee.

---

## 1. DNS with DuckDNS

DuckDNS is free, needs no domain purchase, and gives you a hostname that
follows your home IP around. Let's Encrypt issues certificates for
`duckdns.org` subdomains without trouble — it is on the Public Suffix List, so
your subdomain counts as its own registered domain and does not share rate
limits with everyone else using DuckDNS.

1. Sign in at [duckdns.org](https://www.duckdns.org) with any of the listed
   providers.
2. Create a subdomain — say `milena-led`, giving you
   `milena-led.duckdns.org`.
3. Copy the **token** shown at the top of the page.

Install the updater on the server:

```bash
sudo cp deploy/duckdns-update.sh /usr/local/bin/duckdns-update
```

```bash
sudo chmod +x /usr/local/bin/duckdns-update
```

Write the config with your own subdomain and token. It is `chmod 600` because
anyone holding that token can repoint your domain:

```bash
printf 'DUCKDNS_DOMAIN=milena-led\nDUCKDNS_TOKEN=paste-token-here\n' | sudo tee /etc/duckdns.conf
```

```bash
sudo chmod 600 /etc/duckdns.conf
```

Run it once to check it works — it should log `OK`:

```bash
sudo /usr/local/bin/duckdns-update && sudo tail -n 1 /var/log/duckdns.log
```

Then keep it current. `sudo crontab -e`, and add:

```
*/5 * * * * /usr/local/bin/duckdns-update
```

Confirm DNS resolves to your address:

```bash
dig +short milena-led.duckdns.org
```

That must match `curl -s https://api.ipify.org`. If it does not, the updater
has not run or the token is wrong — fix it before going near certbot, because a
certificate request against a stale record just fails.

> Note: unlike a Cloudflare-proxied domain, DuckDNS points straight at your
> house, so your home IP is publicly visible in DNS. For a bedroom LED sign
> that is a fair trade, but it is worth knowing.

---

## 2. Forward ports 80 and 443

On the router, forward both TCP ports to the server's LAN address.

**80 is required even though the site is HTTPS-only.** Let's Encrypt validates
over port 80, and certbot renews every 60 days. Close it later and renewals
break three months from now, which is a miserable thing to debug.

---

## 3. Put the files on the server

The whole site is the `web/` directory. From your PC, in the project folder
(Git Bash on Windows):

```bash
rsync -av --delete web/ tesla@homeserver:/tmp/love-web/
```

Then on the server:

```bash
sudo mkdir -p /srv/love
```

```bash
sudo rsync -a --delete /tmp/love-web/ /srv/love/web/
```

```bash
sudo chown -R www-data:www-data /srv/love
```

No `rsync` on Windows? `scp -r web/ user@host:/tmp/love-web/` works the same for
a first copy.

---

## 4. Get the certificate

Install nginx and certbot if they are not already there:

```bash
sudo apt install nginx certbot
```

Put the **bootstrap** config in place — the real one references certificate
files that do not exist yet, so nginx would refuse to start:

```bash
sudo cp deploy/nginx-love-bootstrap.conf /etc/nginx/sites-available/love
```

```bash
sudo ln -s /etc/nginx/sites-available/love /etc/nginx/sites-enabled/love
```

**Leave the other sites alone.** This block has an explicit `server_name`, so
nginx routes by hostname and your existing sites are untouched. There is no
need to remove `default` — and on a server already hosting other things, doing
so is a good way to break one of them.

```bash
sudo nginx -t && sudo systemctl reload nginx
```

At this point `http://milena-led.duckdns.org` should show the page. It cannot
talk to the matrix yet — that is expected, and it is exactly the failure the
certificate fixes.

Now request the certificate:

```bash
sudo certbot certonly --webroot -w /var/www/html -d milena-led.duckdns.org
```

### If that fails because port 80 is unreachable

Some ISPs block inbound port 80 outright. If certbot times out on the challenge,
switch to DNS-01, which proves ownership by writing a DNS record instead and
needs no open port at all. DuckDNS has a plugin for it:

```bash
sudo apt install python3-pip && sudo pip3 install certbot-dns-duckdns --break-system-packages
```

```bash
sudo certbot certonly --authenticator dns-duckdns --dns-duckdns-token YOUR-TOKEN --dns-duckdns-propagation-seconds 60 -d milena-led.duckdns.org
```

You still need **443** forwarded for the site itself — DNS-01 only replaces the
validation step, not the serving.

---

## 5. Switch on HTTPS

```bash
sudo cp deploy/nginx-love.conf /etc/nginx/sites-available/love
```

Edit the copy on the server to put your own domain in the four places it
appears — `server_name` twice, and the two `ssl_certificate` paths:

```bash
sudo nano /etc/nginx/sites-available/love
```

```bash
sudo nginx -t && sudo systemctl reload nginx
```

If `nginx -t` complains about `http2`, see the comment at the top of that
server block — the directive moved between nginx versions.

Confirm renewals will work:

```bash
sudo certbot renew --dry-run
```

---

## 6. Verify it properly

From any browser:

```bash
curl -sI https://milena-led.duckdns.org | head -n 20
```

You want `HTTP/2 200`, `content-type: text/html`, and **no**
`permissions-policy` header. That last one matters — a `Permissions-Policy`
that omits `bluetooth` disables the API silently, with nothing useful in the
console. If you have one set globally in `nginx.conf`, this site needs an
exception.

Check the JavaScript is served correctly, since a wrong MIME type plus the
`nosniff` header makes Safari refuse the ES modules and the page comes up
blank:

```bash
curl -sI https://milena-led.duckdns.org/js/main.js | grep -i content-type
```

That must say `text/javascript` or `application/javascript`.

Then on the iPhone:

1. Install **Bluefy** from the App Store. Safari has no Web Bluetooth and never
   will — this is not a setting you can turn on.
2. Open your URL in Bluefy, tap **Połącz**, choose `Milena ♥`.
3. Add it to Bluefy's bookmarks so she never has to type the address.

If Połącz says the browser has no Bluetooth support, the certificate is not
being trusted. Check the padlock in Bluefy before looking anywhere else.

---

## Deploying a change later

```bash
rsync -av --delete web/ tesla@homeserver:/tmp/love-web/
```

```bash
sudo rsync -a --delete /tmp/love-web/ /srv/love/web/
```

No nginx reload needed — the files are static. `index.html` is served
`no-cache`, so a change reaches her phone as soon as she reopens the page;
everything else is cached for five minutes.

---

## If you are behind CGNAT

Port forwarding cannot work, but the site still can. Two options that need no
inbound connection at all:

**Cloudflare Tunnel** keeps the site on your server. `cloudflared` makes an
outbound connection to Cloudflare, which terminates TLS with a real certificate
and forwards traffic down that tunnel. Free, no port forwarding, no dynamic
DNS. You point it at `http://localhost:80` and keep the bootstrap config.

**A static host** — GitHub Pages, Cloudflare Pages, Netlify. The site is
entirely static and the server never talks to the matrix, so nothing is lost
functionally. You give up self-hosting, and gain a page that stays up when your
home server reboots.

Either way the firmware, the app, and everything else here is unchanged. Only
where `web/` is served from differs.

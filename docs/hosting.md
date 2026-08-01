# Hosting the web app

The site is static — no build step, no npm, no backend. Hosting it is copying a
directory and pointing nginx at it. The only genuinely fiddly part is the
certificate, and that part is not optional:

> **Web Bluetooth requires a secure context.** `navigator.bluetooth` does not
> exist over plain `http://`, so the Połącz button will report that the browser
> has no Bluetooth support — even in Bluefy, on a page that is otherwise
> working perfectly. A self-signed certificate is usually rejected too.

Throughout, replace `milena.twojadomena.pl` with your domain and `love` with
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

## 1. DNS

Point an A record at your public IP:

```
milena.twojadomena.pl.   A   203.0.113.42
```

If your home IP changes, use a dynamic DNS client (`ddclient`, or whatever your
router offers) so the record follows it. A certificate renewal against a stale
record fails silently until the cert expires.

Wait for it to propagate, then confirm from the server:

```bash
dig +short milena.twojadomena.pl
```

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
rsync -av --delete web/ teslunia@milena.twojadomena.pl:/tmp/love-web/
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

Remove Debian's default site if it would otherwise answer for your domain:

```bash
sudo rm -f /etc/nginx/sites-enabled/default
```

```bash
sudo nginx -t && sudo systemctl reload nginx
```

At this point `http://milena.twojadomena.pl` should show the page. It cannot
talk to the matrix yet — that is expected, and it is exactly the failure the
certificate fixes.

Now request the certificate:

```bash
sudo certbot certonly --webroot -w /var/www/html -d milena.twojadomena.pl
```

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
curl -sI https://milena.twojadomena.pl | head -n 20
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
curl -sI https://milena.twojadomena.pl/js/main.js | grep -i content-type
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
rsync -av --delete web/ teslunia@milena.twojadomena.pl:/tmp/love-web/
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

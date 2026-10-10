#!/usr/bin/env python
"""Tiny SSH helper for the RG40XXV (muOS).

  python dev.py run "<command>"          run a shell command on the device
  python dev.py put <local> <remote>     copy a file to the device
  python dev.py get <remote> <local>     copy a file from the device
  python dev.py setup-key                install ~/.ssh/id_ed25519.pub for passwordless login

Password auth falls back to the muOS default (root/root); set RG_PASS to override.
"""
import os, sys, paramiko

HOST = os.environ.get("RG_HOST", "192.168.1.175")
USER = os.environ.get("RG_USER", "root")
PASS = os.environ.get("RG_PASS", "root")
KEY = os.path.expanduser("~/.ssh/id_ed25519")


def connect():
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        c.connect(HOST, username=USER, key_filename=KEY, timeout=10, look_for_keys=False, allow_agent=False)
    except paramiko.AuthenticationException:
        c.connect(HOST, username=USER, password=PASS, timeout=10, look_for_keys=False, allow_agent=False)
    return c


def run(c, cmd):
    _, out, err = c.exec_command(cmd, timeout=120)
    o, e = out.read().decode(errors="replace"), err.read().decode(errors="replace")
    rc = out.channel.recv_exit_status()
    return rc, o, e


def main():
    a = sys.argv[1:]
    c = connect()
    if a[0] == "run":
        rc, o, e = run(c, a[1])
        sys.stdout.write(o); sys.stderr.write(e); sys.exit(rc)
    elif a[0] == "put":
        # SFTP on this muOS build can't open paths under /opt, so stream through the shell.
        data = open(a[1], "rb").read()
        _, out, err = c.exec_command("cat > '%s'" % a[2].replace("'", "'\\''"))
        out.channel.sendall(data); out.channel.shutdown_write()
        rc = out.channel.recv_exit_status()
        print("put", a[2], len(data), "bytes", "rc=%d" % rc, err.read().decode())
    elif a[0] == "get":
        _, out, _ = c.exec_command("cat '%s'" % a[1].replace("'", "'\\''"))
        open(a[2], "wb").write(out.read()); print("got", a[2])
    elif a[0] == "setup-key":
        pub = open(KEY + ".pub").read().strip()
        rc, o, e = run(c, "mkdir -p ~/.ssh && chmod 700 ~/.ssh && grep -qF '%s' ~/.ssh/authorized_keys 2>/dev/null || echo '%s' >> ~/.ssh/authorized_keys; chmod 600 ~/.ssh/authorized_keys; echo done" % (pub, pub))
        print(o, e)
    c.close()


if __name__ == "__main__":
    main()

import sys, os, paramiko
host, user, pw = "192.168.10.128", "cmi", "zhaorui"
local, remote = sys.argv[1], sys.argv[2]
c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(host, username=user, password=pw, timeout=15)
size = os.path.getsize(local)
stdin, stdout, stderr = c.exec_command(f"cat > {remote}", timeout=600)
with open(local, "rb") as f:
    while True:
        b = f.read(1 << 20)
        if not b:
            break
        stdin.write(b)
    stdin.flush()
    stdin.channel.shutdown_write()
rc = stdout.channel.recv_exit_status()
err = stderr.read().decode(errors="replace")
print(f"PUT {local} -> {remote} ({size} bytes) rc={rc}")
if err.strip():
    print("[stderr]", err[:500])
c.close()

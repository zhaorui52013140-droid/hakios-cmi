import sys, os, paramiko

host, user, pw = "192.168.10.128", "cmi", "zhaorui"
c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(host, username=user, password=pw, timeout=15)
s = c.open_sftp()

cmd = sys.argv[1]
if cmd == 'get':
    remote, local = sys.argv[2], sys.argv[3]
    os.makedirs(os.path.dirname(local), exist_ok=True)
    s.get(remote, local)
    print(f"GET {remote} -> {local} ({os.path.getsize(local)} bytes)")
elif cmd == 'put':
    local, remote = sys.argv[2], sys.argv[3]
    s.put(local, remote)
    print(f"PUT {local} -> {remote} ({os.path.getsize(local)} bytes)")
elif cmd == 'ls':
    for a in s.listdir_attr(sys.argv[2]):
        print(f"{a.st_mode:o} {a.st_size:>10} {a.filename}")
s.close()
c.close()

#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
share_name=aranym-share
share_path="$root/lan-share"
config=/etc/samba/smb.conf
backup=/etc/samba/smb.conf.aranym-backup

if [ ! -d "$share_path" ]; then
	mkdir -p "$share_path"
fi

if ! command -v smbd >/dev/null 2>&1; then
	echo "Samba is not installed. Install the samba package first." >&2
	exit 1
fi

if ! sudo test -f "$backup"; then
	sudo cp "$config" "$backup"
fi

share_user=${SUDO_USER:-$(id -un)}

if ! sudo grep -q "^\[$share_name\]$" "$config"; then
	sudo tee -a "$config" >/dev/null <<EOF

[$share_name]
	comment = ARAnyM shared transfer folder
	path = $share_path
	browseable = yes
	read only = no
	guest ok = no
	valid users = $share_user
	force user = $share_user
	create mask = 0664
	directory mask = 0775
EOF
fi

echo "Set the Samba password for user '$share_user'."
sudo smbpasswd -a "$share_user"
sudo testparm -s >/dev/null
sudo systemctl enable --now smbd

host=$(hostname -s)
printf 'Share ready: \\\\%s\\%s\n' "$host" "$share_name"
echo "Open that path from Windows File Explorer."

#!/usr/bin/env python3
"""Create a local signing key once. Private material stays out of Git."""
import os
from pathlib import Path
import secrets
import subprocess

directory = Path(__file__).resolve().parents[1] / ".signing"
directory.mkdir(mode=0o700, exist_ok=True)
key = directory / "asm101-release.jks"
password_file = directory / "password"
if key.exists():
    if not password_file.exists():
        raise SystemExit("Signing key exists but its local password file is missing.")
    print("Using existing local release key.")
else:
    password = password_file.read_text().strip() if password_file.exists() else secrets.token_urlsafe(36)
    password_file.write_text(password + "\n")
    password_file.chmod(0o600)
    environment = os.environ.copy()
    environment["ASM101_STORE_PASSWORD"] = password
    subprocess.run([
        "keytool", "-genkeypair", "-keystore", str(key), "-storetype", "PKCS12",
        "-storepass:env", "ASM101_STORE_PASSWORD", "-keypass:env", "ASM101_STORE_PASSWORD",
        "-alias", "asm101", "-keyalg", "RSA", "-keysize", "3072", "-validity", "10000",
        "-dname", "CN=ASM101, OU=Android, O=Luis Pinto, C=PT",
    ], env=environment, check=True)
    key.chmod(0o600)
    print("Created local release key. Back up android/.signing privately for future updates.")

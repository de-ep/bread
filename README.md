# Bread

Bread is a multithreaded encryption daemon that monitors the specified directory for files that are moved into it. Upon detecting a new file, the daemon encrypts the file using OpenPGP standard, in case a directory is moved into monitored directory, the daemon encrypts the files present inside the directory recursively.
**Note: The daemon only processes files that are moved into the directory, not those that are copied.**

The logs can be found in bread.log file in monitored directory.

## Features:

- **Secure decryption key**: Files are encrypted using a public key, ensuring that the decryption key remains protected and secure.
- **Cryptographically secure encryption**: The daemon uses the GPGME library, which provides a high-level interface to GnuPG, ensuring that the encryption is cryptographically secure.
- **High performance**: The daemon is multithreaded, ensuring high-speed processing and efficient handling of multiple file encryption tasks simultaneously.


## Getting started: 

### Installing gpgme: 
On debian: 
```bash
sudo apt install libgpgme-dev
```
On arch: 
```bash
sudo pacman -S gpgme
```

### Building project: 
```bash 
git clone https://github.com/de-ep/bread.git
cd bread 
make 
```

### Usage 
```bash
./bread ~/protected_dir ABCD1234
```
- **~/protected_dir**: The directory to monitor for moved files.
- **ABCD1234**: The fingerprint (or key ID) associated with the public key for encryption.

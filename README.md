# Portable HTTP Download Manager
A very fast and efficient multi-thread HTTP download manager for Windows, implemented with purely C++/Windows API.
* Uses less than 5MB memory after startup, less than 8MB memory for typical file download with 5 threads.
* HTTP and HTTPS support.
* Cookie and system default proxy support.
* HTTP authentication support.
* Able to download only a specific section of a file, great for repairing corrupted files by avoiding redownloading the whole content again.
* Dynamic and intelligent download thread creation to fully ulitise network bandwidth for fastest download speed.

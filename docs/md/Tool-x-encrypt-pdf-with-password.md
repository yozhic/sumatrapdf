# Encrypt a PDF with password

**Available in [pre-release 3.7](https://www.sumatrapdfreader.org/prerelease)**

To encrypt a PDF with password using SumatraPDF from command-line:

`SumatraPDF clean -E aes-256 -U pwd foo.pdf foo-encrypted.pdf`

Flags:

- `-E` : encryption algorithm
- `-U <pwd>` : password required to open PDF file

You can see [all flags](Tool-clean.md).

You can also [decrypt](Tool-x-decrypt-pdf.md)

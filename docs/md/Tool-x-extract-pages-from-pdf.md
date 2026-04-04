# Extract pages from PDF

**Available in [pre-release 3.7](https://www.sumatrapdfreader.org/prerelease)**

To extract pages from a PDF using SumatraPDF on a command line:

`SumatraPDF clean input.pdf output.pdf 1,2-5,8-N`

This extracts pages 1, 2, 3, 4, 5 and 8 to last from `input.pdf` and saves as `output.pdf`

`N` is special character meaning "last page".

## Delete page or pages from PDF

To delete a page you need to extract all pages except the one you want to delete. See [delete pages from PDF](Tool-x-delete-pages-from-pdf.md).

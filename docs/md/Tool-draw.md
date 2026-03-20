# SumatraPDF draw

**Available in [pre-release 3.7](https://www.sumatrapdfreader.org/prerelease)**

```
usage: SumatraPDF draw [options] file [pages]
  -p -    password

  -o -    output file name (%d for page number)
  -F -    output format (default inferred from output file name)
          raster: png, pnm, pam, pbm, pkm, pwg, pcl, ps, pdf, j2k
          vector: svg, pdf, trace, ocr.trace
          text: txt, html, xhtml, stext, stext.json
          ocr'd text: ocr.txt, ocr.html, ocr.xhtml, ocr.stext, ocr.stext.json (disabled)
          bitmap-wrapped-as-pdf: pclm, ocr.pdf

  -q      be quiet (don't print progress messages)
  -s -    show extra information:
          m - show memory use
          t - show timings
          f - show page features
          5 - show md5 checksum of rendered image

  -R -    rotate clockwise (default: 0 degrees)
  -r -    resolution in dpi (default: 72)
  -w -    width (in pixels) (maximum width if -r is specified)
  -h -    height (in pixels) (maximum height if -r is specified)
  -f      fit width and/or height exactly; ignore original aspect ratio
  -b -    use named page box (MediaBox, CropBox, BleedBox, TrimBox, or ArtBox)
  -B -    maximum band_height (pXm, pcl, pclm, ocr.pdf, ps, psd and png output only)
  -T -    maximum number of rendering threads (banded mode only, limited to number of bands per page)

  -W -    page width for EPUB layout
  -H -    page height for EPUB layout
  -S -    font size for EPUB layout
  -U -    file name of user stylesheet for EPUB layout
  -X      disable document styles for EPUB layout
  -a      disable usage of accelerator file

  -c -    colorspace (mono, gray, grayalpha, rgb, rgba, cmyk, cmykalpha, filename of ICC profile)
  -e -    proof icc profile (filename of ICC profile)
  -G -    apply gamma correction
  -I      invert colors

  -A -    number of bits of antialiasing (0 to 8)
  -A -/-  number of bits of antialiasing (0 to 8) (graphics, text)
  -l -    minimum stroked line width (in pixels)
  -K      do not draw text
  -KK     only draw text
  -D      disable use of display list
  -i      ignore errors
  -m -    limit memory usage in bytes
  -L      low memory mode (avoid caching, clear objects after each page)
  -P      parallel interpretation/rendering
  -N      disable ICC workflow ("N"o color management)
  -O -    Control spot/overprint rendering
            0 = No spot rendering
            1 = Overprint simulation (default)
            2 = Full spot rendering
  -t -    Specify language/script for OCR (default: eng) (disabled)
  -d -    Specify path for OCR files (default: rely on TESSDATA_PREFIX environment variable) (disabled)
  -k -{,-}        Skew correction options. auto or angle {0=increase size, 1=maintain size, 2=decrease size} (disabled)
  
  -y l    List the layer configs to stderr
  -y -    Select layer config (by number)
  -y -{,-}*       Select layer config (by number), and toggle the listed entries

  -Y      List individual layers to stderr
  -z -    Hide individual layer
  -Z -    Show individual layer

  pages   comma separated list of page numbers and ranges
```

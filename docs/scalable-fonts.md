# Scalable TTF fonts on ESP32-S3 devices

On ESP32-S3 readers, CrossInk includes Bitter and Lexend Deca as scalable
TrueType fonts. You can also add your own static `.ttf` fonts. ESP32-C3 readers
continue to use the existing bitmap fonts and `.cpfont` font packs.

## Add your own TTF fonts

1. Copy your `.ttf` files into a family folder on the SD card, such as
   `/fonts/My Font/`. The hidden `/.fonts/My Font/` location works too.
2. If you copied the files using a computer, restart the reader or refresh its
   font list.
3. Choose the family from **Settings > Reader > Font Options > Font Family**, or
   from the font picker while reading.

You can also upload `.ttf` files through the **Fonts** tab in the reader's web
interface. See [SD Card Fonts](./sd-card-fonts.md) for how to open that interface.
No font conversion or separate files for each size are needed.

For the full range of text styles, add all four files for a family: **regular**,
**bold**, **italic**, and **bold italic**. CrossInk can use a family with fewer
styles, but missing styles may appear in another available style. If the files
are in a family folder, that folder's name appears in the font picker. Keep
`.ttf` and `.cpfont` files in separate family folders.

## Adjust the appearance

After selecting a custom TTF family, open **TTF Rendering** under **Settings >
Reader > Font Options**, or from the in-book font menu. These controls let you
adjust how the letters look on the e-ink screen. Changes to a family apply to
all its styles and are remembered for that family. This option appears only
for custom TTF fonts; built-in and `.cpfont` fonts do not use it.

Custom TTF fonts use the existing **8–22 pt** size choices. Changing fonts or
their appearance may make CrossInk lay out the current book again, so page
positions can change. Your bookmarks and clippings remain saved.

## Supported files and limits

- Use static TrueType `.ttf` files. Variable fonts, `.otf` fonts, and font
  collections are not supported.
- Each file must be **2 MiB or smaller**, and the four files in one family must
  total **6 MiB or less**.
- If both `/fonts` and `/.fonts` contain the same family, the copy in
  `/.fonts` takes precedence.

If a font does not appear, check its file type and size, then refresh the font
list or restart the reader. If the reader has too little free memory to open a
font, it may use a fallback font instead.

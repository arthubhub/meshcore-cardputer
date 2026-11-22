# Custom themes

Copy a theme file to the `themes` folder of the microSD card (`/themes/ocean.txt`), restart the Cardputer, then choose it in
*Settings → Theme*. Up to 8 themes are read from the card; the built-in ones are Dark, Light, High contrast, Night and Amber.

A theme file is plain text, one `key = #RRGGBB` per line, `#` at the start of a line for a comment. Every key is optional:
what is left out comes from the `base` theme (Dark by default).

| key | colour of |
|---|---|
| `name` | the name shown in the settings (default: the file name) |
| `base` | the built-in theme the file starts from: `Dark`, `Light`, `High contrast`, `Night` or `Amber` |
| `background` | the screen |
| `text` | most text |
| `dim` | secondary text (times, hints) |
| `accent` | titles, values, the compose frame |
| `ok`, `warning`, `bad` | delivered / warnings / errors |
| `bar` | the top bar and pop-ups |
| `selection` | the selected row |
| `bubble_in`, `bubble_out` | received / sent message bubbles |
| `mention`, `mention_me` | `@name` highlights; `@me` is drawn with the background colour as text |

The serial log names any line it could not use, and warns when two colours are too close to read (text needs a
contrast of 4.5:1 with its background, secondary text 3:1).

`ocean.txt` and `forest.txt` in this folder are examples.

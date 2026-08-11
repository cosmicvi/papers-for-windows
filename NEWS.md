Papers 51.beta
--------------

* Potentially of interest to packaging and other downstreams might be:
  - crate geo is no longer needed
  - crate imageproc is now required to process signature images

* Some changes we can highlight:
  - Ability to insert visual signatures within Papers (Malika Asman)
  - #635 More details on paper sizes (Mark Tkachov)
  - Use new gtk snapping API for fractional scaling (balooii)

* Bug fixes:
  - #705 Fix a crash when font names are not UTF-8 (Qiu Wenbo)
  - #712 crate geo is no longer needed (Myriade)
  - Fix GNUC_CONST usage (Maximiliano)
  - #715 Do not reset the cursor page on focus (Lukáš Tyrychtr)
  - Several fixes for DJVU documents (Qiu Wenbo)

* Other changes:
  - Free text annotations are now printed by default (Lucas Baudin)
  - More properties are now exposed in PpsView for apps using Papers as a library (Peter Eisenmann)

Papers 51.alpha
---------------

* Bug fixes:
  - Fix build with latest gtk versions (Jordan Petridis)
  - Adjust symbolic stroke icons to be compatible with GTK >= 4.22 (balooii)
  - Add missing tooltips in annotations toolbar (Lukáš Tyrychtr)
  - Default settings: don't allow documents to change zoom level by default (Lucas Baudin)
  - Center free text annotations on insertion (Lucas Baudin)
  - #656 Correctly detect some checkboxes (Lucas Baudin)
  - #640 Save and restore window size and maximized state in the previewer (sajal)
  - Improved AppData keywords (Jeff Fortin)
  - #324 Rename "Creator" and "Producer" PDF metadata properties fields labels to make them clearer (Lucas Baudin)
  - #527, #609 Improve layout for multiple signatures in properties dialog (Philipp Klein, rukysandy)
  - #631 Don't link buttons in document properties (omprakash choudhary)
  - #603 Free text annotations improvements (Rakesh)

* Bug fixes already released in the 50.x serie:
  - #610 PDF link jumps are off target for some files (Nelson Ben)
  - #597 Scrolling stalls image loading for image-heavy PDFs and CBZs with fractional scaling enabled (balooii)
  - #340 Pinch to zoom sometimes causes huge page jumps (balooii)
  - #642 Text jitter on fractional scaling (balooii)
  - #685 Pencil size icons look broken (balooii)
  - !764 shell: escape link arguments before spawning a new process, related to CVE-2026-46529 of Evince (Lucas Baudin)
  - !766 build: Include gdk.h instead of gdkkeysyms.h (Jordan Petridis)
  - !760 Fix null pointer dereference in PpsPrintOperationPrint (Casey)
  - !754 Chain up constructed vfuncs (Maximiliano)
  - !739 shell: Fix crash when navigating empty search results (Lennard Hofmann)
  - !718 libview: Correct zoom in odd left dual page mode (Colin Kinloch)
  - #655 text annotation displayed in wrong colors (Nelson Ben)
  - #632 Scrolling in presentation mode skips pages (Qiu Wenbo)
  - !747 libview: Fix tool not pointing towards the ink in annotation cursor (Alessandro Astone)
  - !748 shell: save load job and handler id when reloading (Lucas Baudin)
  - !746 fix several criticals for the previewer (Lucas Baudin)
  - #669 libview: fix free text annotations position on insertion (Lucas Baudin)
  - #656 libview: fix detection of some form checkboxes (Lucas Baudin)

* Translation updates:
  - Belarusian (Vasil Pupkin)
  - Dutch (Nathan Follens)
  - French (rene-coty)
  - German (Christian Kirbach)
  - Italian (Davide Ferracin)
  - Norwegian Bokmål (Kjartan Maraas)
  - Occitan (Quentin PAGÈS)
  - Polish (Victoria Niedzielska, Daniel)
  - Persian (Danial Behzadi)
  - Romanian (Antonio Marin)
  - Servian (Марко М. Костић)
  - Slovak (Jose Riha)
  - Slovenian (Martin S.)
  - Thai (Aefgh Threenine)
  - Turkish (emintufan)
  - Vietnamese (Cas Pascal)


Papers 50
--------------
* Bug fixes:
  - #517 Saved image files are empty (Lucas Baudin)
  - #536 Print dialog says "Manage Custom Sizes" for Paper Size every time (Prasun Gera)

* Translation updates for the 50.0 cycle:
  - Basque (Asier Saratsua Garmendia)
  - Brazilian Portuguese (Álvaro Burns)
  - British English (Bruce Cowan)
  - Bulgarian (Alexander Alexandrov Shopov, twlvnn kraftwerk)
  - Catalan (Jordi Mas i Hernandez, Victor Dargallo)
  - Chinese (China) (Luming Zh)
  - Chinese (Taiwan) (Cheng-Chia Tseng)
  - Czech (Daniel Rusek)
  - Danish (Alan Mortensen)
  - Esperanto (Kristjan Schmidt)
  - Finnish (Jiri Grönroos)
  - French (Lucien Ouoba, Malo Billoré)
  - Friulian (Fabio Tomat)
  - Galician (Fran Diéguez)
  - Georgian (Ekaterine Papava)
  - German (Jürgen Benvenuti)
  - Greek (Efstathios Iosifidis)
  - Hebrew (Yaron Shahrabani)
  - Hungarian (Balázs Úr)
  - Indonesian (Andika Triwidada)
  - Italian (Davide Ferracin)
  - Japanese (Makoto Sakaguchi, Oyamada Jun)
  - Kazakh (Baurzhan Muftakhidinov)
  - Korean (Seong-ho Cho)
  - Lithuanian (Aurimas Černius)
  - Norwegian Nynorsk (Sunniva Løvstad)
  - Occitan (Quentin PAGÈS)
  - Persian (Danial Behzadi)
  - Portuguese (Hugo Carvalho)
  - Romanian (Antonio Marin)
  - Russian (Artur S0)
  - Serbian (Милош Поповић)
  - Serbian (Latin) (Милош Поповић)
  - Slovenian (Martin)
  - Spanish (Daniel Mustieles, Nahué Mantuani)
  - Swedish (Anders Jonsson)
  - Thai (Aefgh Threenine)
  - Turkish (Sabri Ünal, Emin Tufan Çetin)
  - Uighur (Abduqadir Abliz)
  - Ukrainian (Yuri Chornoivan)

Papers 50.rc
--------------
* Some changes we can highlight:
  - selection of annotation color is now remembered (Lucas Baudin)

* Bug fixes:
  - #626 Right clicking highlighted text changes its highlight colour (Lucas Baudin)

Papers 50.beta
--------------

* Potentially of interest to packaging and other downstreams might be:
  - The MSRV is now bumped to 1.85

* Some changes we can highlight:
  - new editing tools (ink and free text annotations) (Lucas Baudin)
  - better troubleshooting information (Adithya)
  - add a "copy email address" menu item (Adithya)
  - improved translation strings using formatx! and gettext (Maximiliano)
  - stop using osd style for overlay buttons

* Bug fixes:
  - show a radio button for digital signing when there is a single certificate (Marek Kašík)
  - fix sidebar not grabbing focus when it should (Roberto Vidal)

Papers 50.alpha
---------------

Some changes we can highlight:
  - Improved UI for annotations (toast for deletions, new menu to edit from the sidebar) (Malika Asman)
  - Various fixes already backported in the 49.x series (crashes, memory leaks, focus improvements, digital signature fixes) (nibon7, Lucas Baudin, Malika Asman, FineFindus, Marek Kašík, Alessandro Astone)
  - Announcing search results to screen readers (Lukáš Tyrychtr)

Papers 49.0
----------------
* Bug fixes:
  - Two use-after-free issues in libview (nibon7)

* Translation updates for the 49.0 cycle:
  - Basque (Asier Saratsua Garmendia)
  - Brazilian Portuguese (Álvaro Burns, Juliano de Souza Camargo)
  - British English (Bruce Cowan)
  - Catalan (Jordi Mas, Jordi Mas i Hernandez)
  - Chinese (China) (Luming Zh)
  - Czech (Daniel Rusek)
  - Danish (Alan Mortensen)
  - Esperanto (Kristjan Schmidt)
  - Finnish (Jiri Grönroos)
  - Galician (Francisco Diéguez Souto)
  - Georgian (Ekaterine Papava)
  - Hebrew (Yaron Shahrabani)
  - Hungarian (Balázs Úr)
  - Italian (Davide Ferracin)
  - Japanese (Makoto Sakaguchi)
  - Korean (Seong-ho Cho)
  - Lithuanian (Aurimas Černius)
  - Occitan (Quentin Pagès)
  - Persian (Danial Behzadi)
  - Portuguese (Hugo Carvalho)
  - Romanian (Antonio Marin)
  - Russian (Artur S0)
  - Slovak (Dušan Kazik)
  - Slovenian (Martin)
  - Spanish (Nahué Mantuani)
  - Swedish (Anders Jonsson)
  - Turkish (Sabri Ünal, Emin Tufan Çetin)
  - Ukrainian (Yuri Chornoivan)

Papers 49.rc
----------------
* Some changes we can highlight:
  - Various memory leak fix (nibon7)
  - Simplify section names in shortcuts dialog (Urtsi Santsi)
  - Switch to image-rs in thumbnailer due to sandbox limitation (lbaudin)

Papers 49.beta
----------------
* Potentially of interest to packaging and other downstreams might be:
  - The MSRV is now bumped to 1.83
  - We now requires meson >= 1.80

* Some changes we can highlight:
  - Switch to use AdwShortcutDialog (Urtsi Santsi)
  - Screen reader support (Lukáš Tyrychtr, lbaudin)
  - UI rework of annotation properties dialog (Qiu Wenbo)
  - Renamed to Document Viewer (Daniel Rusek)
  - Save on CTRL+SHIFT+S as well (Markus Göllnitz)

Papers 49.alpha
----------------
* Some changes we can highlight:
  - Improved annotation UI logic (Pablo Correa Gomez)
  - Better support for fractional scaling (lbaudin)
  - Performance optimizations for selection (lbaudin)
  - Migrate to Blueprint (Jamie Gravendeel)
  - Port thumbnailer to Rust (FineFindus)
  - Move document mutex locks to the backend (Ahmed Fatthi)
  - PpsDocumentPoint refactor (Markus Göllnitz)
  - Add spell checking support for multiline form fields (nibon7)
  - Various search improvements (Hari Rana)
  - Add troubleshooting page to the About dialog (Arujjwal Negi)

* Potentially of interest to packaging and other downstreams might be:
  - The `viewer` meson option is renamed to `shell`

Papers 48.0
----------------

* Some changes we can highlight:
  - Papers is now a lot more stable during reloads, due to multiple race
    and memory-safety bugs fixed.
  - Fullscreen mode at startup works now more reliably

* Bugs fixed:
  - #362 replace pending_scroll/pending_resize/pending_point machinery in
    PpsView (Markus Göllnitz)
  - #376 assumes http instead of https (Qiu Wenbo)
  - #384 Links in Non Continuos Mode Do Not Jump to the Correct Position (Markus
    Göllnitz)

* Translation updates:
  - Belarusian (Vasil Pupkin)
  - Bulgarian (Alexander Shopov)
  - Greek, Modern (1453-) (Giannis Antypas)
  - Hungarian (Balázs Úr)
  - Russian (Artur S0)
  - Spanish (Nahue Mantuani)
  - Swedish (Anders Jonsson)

Papers 48.rc
----------------

* Potentially of interest to packaging and other downstreams might be:
  - Update minimum poppler requirement to 25.01.0. This should not be an issue
    to most, since due to a CVE most people already updated
  - Given the lack of downstream users, there were several API and ABI breaking
    changes in the libraries, but sonames were not updated
  - rust: Raise MSRV to 1.75
  - Add sysprof build feature

* Some changes we can highlight:
  - Multiple improvements to focus handling
  - Improve Search UX deciding when does the sidebar and its selection should
    activate
  - Fix signing that broke in 48.beta

* Bugs fixed:
  - #94 When searchbar's searchentry has an existing query but is deselected,
    Ctrl+F should refocus and select-all instead of clearing the search (Roberto
    Vidal)
  - #156 Search does not get cleared nor cancelled when pressing `Esc`, keeps
    eating CPU in the background (Markus Göllnitz)
  - #180 Triple Click to select the line does not work (Markus Göllnitz)
  - #348 Digital signature broken in 47.3 (Lucas Baudin)
  - #349 "Search" needs a comment (Pablo Correa Gómez)
  - #350 Variables needed in string (Pablo Correa Gómez)
  - #355 Digital signatures cannot find certificate (Qiu Wenbo)
  - #359 sidebar automatically collapses while searching (Markus Göllnitz)
  - #360 Selecting Text Downwards With The Mouse Doesn't Auto Scroll Down
    (Roberto Vidal)
  - #361 Enable system-wide PDF thumbnail generation (Markus Göllnitz)
  - #363 PDF metadata title does not seem to be prioritized over filename in
    window manager title (Pablo Correa Gómez)
  - #364 Crash when opening files on MTP devices (Markus Göllnitz)
  - #366 Crash when opening MoFo's 2014 IRS 990 form Public Disclosure Copy
    (Markus Göllnitz)
  - #367 New (unopened before) documents open to the last page instead of the
    first page (Markus Göllnitz)
  - #368 Opening About Papers crashes the application due to a CRITICAL (Markus
    Göllnitz)
  - #369 Double-click and drag doesn't select by word as it should (Markus
    Göllnitz)
  - #374 Highlighting text that was selected via triple click does not work
    (Markus Göllnitz)

* Translation updates:
  - Bulgarian (twlvnn kraftwerk)
  - Catalan (poc senderi)
  - Chinese (China) (Luming Zh)
  - Czech (Daniel Rusek)
  - Danish (Alan Mortensen)
  - Finnish (Jiri Grönroos)
  - Georgian (Ekaterine Papava)
  - Hebrew (Yosef Or Boczko, Yaron Shahrabani)
  - Hindi (Scrambled 777)
  - Nepali (Pawan Chitrakar)
  - Occitan (post 1500) (Quentin PAGÈS)
  - Persian (Danial Behzadi)
  - Portuguese (Hugo Carvalho)
  - Portuguese (Brazil) (Daniel Dias Rodrigues, Álvaro Burns, Rafael Fontenelle)
  - Russian (Artur S0)
  - Slovenian (Martin)
  - Spanish (Nahue Mantuani, Daniel Mustieles)
  - Turkish (Sabri Ünal)
  - Ukrainian (Yuri Chornoivan)

Papers 48.beta
----------------

Potentially of interest to packaging and other downstreams might be:
* The location of the shell and the Rust bindings changed after the shell's Rust
  port was completed.
* Papers minimum required Poppler version did not change, however to support
  free text annotations Poppler >= 25.02 is required with a detection at build
  time only.
* The PostScript and XPS document formats have been dropped. Papers has a
  modular plugin system that allows implementing support for those formats
  out-of-tree. If there are users still interested in those formats, feel
  free to reach out to us, so we can help you setup maintenance
  out-of-tree, and make sure we don't break them. However, we recommend
  transforming any old documents you might still have in those formats, to
  other more appropriate and moderns formats still read by the Papers'
  default configuration. For PostScript, there has been a previous discussion
  to which you can contribute (#151). For XPS, there has been no previous public
  consideration, but the format seems to never have gained traction and is
  deprecated elsewhere.
  (https://gitlab.gnome.org/GNOME/papers/-/merge_requests/367#note_2276634)

Some changes we can highlight:
* simplified printing job handling relying more on GTK's implementation (!339, Qiu Wenbo)
* After supporting digitially signing in 47, we now have the counterpart of
  digital signature verification. (!299, Jan-Michael Brummer; !357, Marek Kašík)
* Redesign of annotation windows (!356, Om Thorat)
* improved context menu for annotation (!338, Qiu Wenbo; !372, Pablo Correa Gomez)
* replaced caret mode confirmation dialog with toast (!365 & !368, Om Thorat)
* Create text annotations in a single action (!351, Pablo Correa Gomez)
* removed bookmarks sidebar completely (!396, Qiu Wenbo) It was unmaintained,
  did not follow current mock-ups, and the way bookmarks were stored locally
  had multiple issues. Both UI and backend would need to be written from scratch.
  If you want this, get in touch with us.
* The night mode now preserves hue and only adjust the luminosity. From now on
  this is no longer a per-document setting is retained across open and future
  windows. (!380, Philipp Jungkamp; !419, Qiu Wenbo)
* finished Rust port of shell (!398, !146, !405, !406, !414, !429, Qiu Wenbo)
* supporting free text and stamp annotations (!397, lbaudin)
* add support for libspellig (!288, Qiu Wenbo)
* update attachment sidebar UI (!293, Markus Göllnitz)

Fixes:
* #262: text selection does not copy content with middle mouse button (!362, nibon7)
* #263: support help action in Flatpak's bubblewrap sandbox (!411, Maximiliano)
* handle switching between fullscreen and presentation mode correctly (!425, nibon7)
* #332: nearest search result not scrolled to if it's the first one (!441, Pablo Correa Gomez)

Papers 47.0
--------------

Papers has seen mostly maintenance work (fixing issues) and internal
refactorings since 47.rc. Some changes we can highlight:

* There is now information on how to create MacOS builds under the
  build-aux folder (Qiu Wenbo)
* It is now possible to sign documents with digital certificates, like
  those stored in the national IDs of states like Estonia or Spain
  (Jan-Michael Brummer)
* Url parsing in commandline arguments now supports RFC 8118 for URI parsing
  (Qiu Wenbo)
* Windows support has been dropped due to the lack of a maintainer
  (Pablo Correa Gomez)

* Bugs fixed:
- #3 Drop Windows support if there's no maintainer (Pablo Correa Gómez)
- #160 Consider prioritizing ToC (table of contents) over thumbnails as
   primary view in the sidebar, when available (Pablo Correa Gómez)
- #211 Increase page cache size for modern computers' RAM capacity (avoid re-
  processing thumbnails and pages when scrolling up/down) (Qiu Wenbo)
- #222 Feature: When opening links to PDF file with `#page=4` parameter set,
  scroll to page 4 automatically (Qiu Wenbo)
- #235 instant crash on open with open URI portal (Markus Göllnitz)

* Translation updates:
- Basque (Asier Sarasua Garmendia)
- Belarusian (Vasil Pupkin)
- Chinese (China) (Luming Zh)
- Danish (Alan Mortensen)
- Georgian (Ekaterine Papava)
 - German (Jürgen Benvenuti)
- Hebrew (Yosef Or Boczko, Yaron Shahrabani)
- Hungarian (Balázs Úr)
- Persian (Danial Behzadi)
- Slovenian (Martin)
- Swedish (Anders Jonsson)
- Ukrainian (Yuri Chornoivan)

Papers 47.rc
--------------

Papers has seen mostly maintenance work (fixing issues) and internal
refactorings since 47.beta. Some changes we can highlight:

meson:
* Remove "platform" build option, as not needed

libppsview:
* Many of the helper classes for search, bookmarks, attachments, etc. have
  been moved from shell to the view, so that integration logic can be further
  cleaned up
* Zooming with a mouse or touchpad now has a lot more room, and requires less
  precision

shell:
* The sidebar a document is opened at is now set based on the last opened
  document, and not on metadata stored on the file itself. This solves several
  issues with document opening


Papers 47.beta
---------------

Papers has seen mostly changes in the libraries and refactorings since 47.alpha
For packagers:

* We now require libadwaita 1.6.beta and GTK 4.15.2
* We have changed the flatpak permissions to read local files. This is necessary
to reload the document on changes regardless of the sandbox type in use
* The "introspection" configure option is now a feature instead of a boolean
* The "gtk_doc" configure option has been renamed to "documentation"

Other changes we can highlight:

libppsview:
* The gesture management of the document view has been heavily refactored.
Previously, the gesture tracking was done manually, while we are now heavily
relying on GTK4's gesture handling. This makes the experience on touchscreens
a lot more polished
* Drag and dropping into the view is no longer supported

shell:
* The Escape key now works with most dialogs
* Implement modern mockups for the annotations sidebar
* Implement changing color of highlight annotation on creation
* Modify default yellow color for annotations
* Use AdwSpinner instead of GtkSpinner where appropriate


Papers 47.alpha
---------------

Papers has continued with a strong development pace. Most remarkable changes
since the previous release is that the UI now fits on narrow screens! For
packagers:

* We now require libadwaita 1.5.0 and GTK 4.15.1

In addition to a multitude of bug fixes and refactors, we can highlight:

libppsview:
* PpsView does no longer send a signal on annotation change. Instead consumers
  are expected to use the properties of the annotation itself

shell:
* Split the header bar in two according to mockups
* Place the search locally into the sidebar instead of having a specific
  toolbar
* Remove the zoom selector and add instead zoom overlay buttons
* Remove dbus interface for PpsWindow, since we have no use for it anymore.
  There is now only the interface for PpsApplication, as we are looking into
  a future with a single application instance
* Ported several widgets to Rust
* Use AppStream data to build the about dialog
* Add "Open With" action to open the document with an alternative application
* Port multiple dialogs to AdwDialog


Papers 46.1
--------------
* Update flatpak dependencies and manifest name
* Fix gtk requirement in packageconfig files


Papers 46.0
---------------

Papers has been forked from Evince, including the whole history of the
evince-next branch to which the authors of the fork contributed extensively.
Papers has landed the GTK4 port from Qiu Wenbo, many (breaking) cleanups to
both libraries, and a bunch of cleanups, modernizations and improvements to
the application itself. For packagers:

* The build now requires rust
* synctex support has been dropped (possibly temporarily)
* DVI backend has been removed

As of the changes in general, we can highlight:

libraries (libppsdocument, libppsview):
  * Due to the of modernization of these, all deprecated API has been removed.
    There have also been some API-breaking changes all around the code, mostly
    aimed at implementing new patterns. The goal of the authors is to reduce the
    public surface of the libraries. So please come in contact if you are a user
    and want to continue using the libraries long-term. We want to learn from
    your usage to provide a good and long-term maintainable experience.
  * The libraries no longer provide any icons or use any custom ones.
  * EvDocumentModel dual-page property has been removed
  * Transition backends to use a new interface for registration
  * Mark types as derivable or final as need be
  * Rename and deprecate EvJobs that do rendering based on cairo
  * Drop EvJobLoadStream and EvJobLoadGFile. Merge EvJobLoad and EvJobLoadFd
  * Some work on sanity of PpsJobs API.
  * GPU based transition of presentation mode with additional effects support.
    Transitions will no longer work if HW acceleration is not supported
  * Use GThreadPool for PpsJobScheduler. Many jobs could now run concurrently
    (even though they don't yet do to locking issues (see #107) ), and
    we no longer leak the thread memory.
  * PPS_PROFILE and PPS_PROFILE_JOBS environment variables for profiling are now
    ignored. Anybody wishing to profile papers can now use sysprof, which
    provides a far better interface.
  * Use new GtkFileDialog API. As a future goal it is considered to drop the GTK
    dependency of libppsdocument
  * Rename pps_job_is_failed to pps_job_is_succeeded
  * PpsAnnotation fields are now private and not part of the API. Please use
    getters and setters to interact with them.

shell:
  * Port to GTK4 thanks to the effort of Qiu Wenbo
  * Removed spell-checking feature. Hopefully will come back through libspelling
  * Implemented some new mockups for the first time in years (thanks Chris!)
  * Lots of small papercuts from the GTK4 migration and other widgets that had
    not received love in a while.
  * Many internal cleanups to use standard widgets, instead of showing and
    hiding internal widgets as different parts of the application change.
  * Ported many widgets to UI templates
  * Make the main widget of PpsWindow an AdwViewStack
  * Start rewriting the UI in Rust, and port several widgets (#21)
  * Rework a big part of the shell with modern widgetry.
  * Fix dbus names to fit app id.
  * Drop the auto-scroll feature for maintainability reasons.
  * Drop papersd daemon. This was an optional feature that took care that
    would only be an instance of every document opened. So for example, clicking
    on an already opened document in Nautilus would move focus to the opened document
    instead of opening a new one. With this change, new documents will be opened
    unconditionally. We plan to bring the old feature back by refactoring some of
    the code, but it requires better isolation of document rendering, so it will
    probably not be available in the next release
  * Remove lots of actions on PpsWindow, and start splitting some of them up
    into a new "doc" action group. Ideally, in the future those will be used
    for a new PpsDocumentView widget.
  * Remove user reload action, documents will be reloaded automatically if
    changes are detected.
  * Warn users about broken metadata
  * Remove annotation toolbar, and only allow creating annotations through
    shortcuts and the context menu.
  * Show a loading view when the document takes long to load, which greatly
    improves the UX.
  * Introduce PpsSearchContext to hold the search, splitting the logic from
    PpsWindow, and getting another step closer to having the logic ready for a
    tabbed window.

properties:
  * Can now be used in Nautilus again. Dropped GTK dependency

backends:
  * Dropped support for DVI file format. This is an old and seldomly used
    document format. The backend has lived unmaintained for many years, and
    we don't have the bandwidth or interest to maintain it. DVI documents
    can always be converted to PDF using Evince or other programs. If anybody
    in the community has a strong interest, they could always maintain an
    OOT backend, or come to us.
  * Dropped synctex support. This was planned to work together with gedit,
    that is no longer a Core App. And modern tex editors all have a PDF
    previewer built-in. So there's no need to duplicate that work when we
    cannot maintain a copy of the synctex library internally.
  * Moved under libdocument

Of course, all this does not come without some issues, so we have likely
regressed in some aspects. We look forward to testers and gathering feedback.

As of the writing, the total diff lays at:

git diff -M -B --stat  fea2b4a8f HEAD -- . ':(exclude)shell-rs/ev-girs/*.gir' ':(exclude)po'
781 files changed, 73778 insertions(+), 107599 deletions(-)

EXTRA_DIST = \
    c-constants-gen.py \
    doc-generator.xsl \
    glib-ginterface-gen.py \
    glib-gtypes-generator.py \
    glib-interfaces-gen.py \
    glib-signals-marshal-gen.py \
    identity.xsl \
    libglibcodegen.py \
    libtpcodegen.py \
    make-release-mail.py \
    telepathy.am

CLEANFILES = *.pyc *.pyo

all: $(EXTRA_DIST)

libglibcodegen.py: libtpcodegen.py
	test -e $<
	$(AM_V_GEN)touch $@

glib-ginterface-gen.py glib-gtypes-generator.py glib-interfaces-gen.py \
glib-signals-marshal-gen.py c-constants-gen.py: %: libglibcodegen.py
	test -e $<
	$(AM_V_GEN)touch $@

TELEPATHY_GLIB_SRCDIR = $(top_srcdir)/../telepathy-glib
maintainer-update-from-telepathy-glib:
	set -e && cd $(srcdir) && \
	for x in $(EXTRA_DIST); do \
		if test -f $(TELEPATHY_GLIB_SRCDIR)/tools/$$x; then \
			cp $(TELEPATHY_GLIB_SRCDIR)/tools/$$x $$x; \
		fi; \
	done

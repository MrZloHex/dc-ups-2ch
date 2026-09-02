# DC-UPS-2CH — LaTeX documentation build
#
# Requires: TeX Live 2020+ (or MiKTeX) with xelatex and latexmk, plus the
# DejaVu font family (part of texlive-fontsextra / DejaVu system fonts).
#
# Targets:
#   make          # same as `make all` — build every PDF
#   make all      # build every PDF listed in TARGETS
#   make doc      # build only the passport / manual
#   make label    # build only the enclosure label
#   make watch    # rebuild on save (needs latexmk -pvc)
#   make clean    # remove intermediate build artefacts, keep the PDFs
#   make distclean# remove intermediates AND the PDFs

LATEXMK    := latexmk
LATEXMKOPTS := -xelatex -interaction=nonstopmode -halt-on-error -file-line-error

# List every top-level .tex here. Anything that gets \include'd is picked
# up automatically by latexmk.
SOURCES := DC_UPS_documentation_v6.tex \
           DC_UPS_label_v6.tex

TARGETS := $(SOURCES:.tex=.pdf)

.PHONY: all doc label watch clean distclean help
.DEFAULT_GOAL := all

all: $(TARGETS)

doc:   DC_UPS_documentation_v6.pdf
label: DC_UPS_label_v6.pdf

%.pdf: %.tex
	$(LATEXMK) $(LATEXMKOPTS) $<

# Continuous rebuild — Ctrl-C to stop.
watch:
	$(LATEXMK) $(LATEXMKOPTS) -pvc $(SOURCES)

# Remove intermediates but keep the finished PDFs so the repo stays useful
# without a TeX toolchain.
clean:
	$(LATEXMK) -c $(SOURCES)
	rm -f *.aux *.log *.out *.toc *.synctex.gz *.fls *.fdb_latexmk \
	      *.bcf *.run.xml *.blg *.bbl *.lof *.lot *.xdv *.nav *.snm \
	      *.vrb *-eps-converted-to.pdf

# Wipe everything, including the PDFs.
distclean: clean
	$(LATEXMK) -C $(SOURCES)
	rm -f $(TARGETS)

help:
	@echo "Targets: all doc label watch clean distclean"
	@echo "Sources: $(SOURCES)"
	@echo "Outputs: $(TARGETS)"

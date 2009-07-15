EXTRA_DIST = \
		G3Bridge/DC_API_Monitor.py \
		G3Bridge/DC_API_SINGLE_Monitor.py \
		G3Bridge/G3BridgeMonitor.py \
		G3Bridge/GridMonitor.py \
		G3Bridge/__init__.py

all-local:
	python setup.py build --build-base=$(top_builddir)/py

install-exec-local:
	python setup.py install --prefix=$(prefix) --root=$(DESTDIR)

clean-local:
	rm -f $(builddir)/G3Bridge/*.pyc

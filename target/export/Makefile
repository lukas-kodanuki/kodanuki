.SILENT:
.PHONY: render viewer clean

SCENE  := scenes/gallery.sdf
CONFIG := target/configs/render_final.toml

render viewer clean:
	target/workflow.py $@ $(SCENE) $(CONFIG)

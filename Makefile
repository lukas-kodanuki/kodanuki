SCENE  := scenes/chess-board.sdf
CONFIG := target/configs/render_final.toml

render viewer export test clean:
	@ target/workflow.py $@ $(SCENE) $(CONFIG)

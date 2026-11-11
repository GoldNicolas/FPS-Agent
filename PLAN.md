Using Unreal MCP we are using claude to make a Valorant Low fidelity clone game/env. Things the training env must have:
    - 1 to 1 layout of valorant maps
    - same movement dynamics and physics
    - same shooting physics and recoils, spread from moving
    - simple character: BEANS WITH GUNS
    - max health, vandal gun, game specs for that gun.
    - training gallery maps bot, their algo

Randomized Map section
Randomized spawn point, in safe areas
Randomized bot spawning 
Randomized bot diff 


Evaluation: Deathmatch between agents in full map.

Train an agents to move and shoot at enemies using parallel envs and RL algo

current reward function: + shooting score + finding bots - damage taken

shooting score is basic: head shot, body shot, per shot ratio
finding bots is based on visual encounter with new bots, so how many bots it finds before it dies.

penalize damage taken so it learns to avoid not being shot at

Documentation to check out: Epic Games Learning Agents 5.5 documentation
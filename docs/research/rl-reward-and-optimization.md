# Reward design and optimization for our PPO agents: research notes

- **Date / side:** 2026-11-12, LOCAL.
- **Purpose:** the reasoning behind the combat-training design ([`../superpowers/specs/2026-11-12-combat-training-design.md`](../superpowers/specs/2026-11-12-combat-training-design.md)).
- **Sources:** each finding says where it comes from: our pinned trainer code, a paper, or a summary of one. Treat figures quoted from summaries as approximate.

## 1. How our trainer actually learns (read from `cluster/learning_agents_python/`, UE 5.7.4)
These are facts about *our* code, not general PPO lore. They matter more than any paper.

| Fact | Where | What it means for us |
|---|---|---|
| Advantages are divided by their standard deviation (not mean-centred), then clipped to `[AdvantageMin, AdvantageMax]` = **`[0, 10]`** by default | `ppo.py` → `compute_advantages_and_returns` | **Only better-than-expected actions are reinforced directly.** Worse-than-expected actions get advantage 0; they are never pushed down. Penalties still matter, because a penalty lowers that action's advantage relative to the others, but positive rewards do most of the teaching. Keep penalties small, and make sure good behaviour has a positive signal. |
| The critic uses an **L1** loss, plus a small return regularizer | `ppo.py` (comment by the author) | The value estimate is robust to the reward scale, so we don't need reward normalization. |
| **No reward normalization or clipping** | `ppo.py`, `train_ppo.py` | The *ratios* between reward terms decide behaviour. Keep per-step rewards roughly within ±1 and episode returns roughly 1–10, so no single term swamps the rest by accident. |
| Truncated episodes bootstrap from the critic; terminated ones don't | `ppo.py` (`terminated` / `truncated` masks) | Use **Termination** for real endings (death, all bots dead) and **Truncation** for the time limit. A timeout is not a failure state in the maths. |
| `DiscountFactor` 0.99, `GaeLambda` 0.95, `ActionEntropyWeight` **0**, LR 1e-4 policy / 1e-3 critic, 32 gradient iterations per batch | `FLearningAgentsPPOTrainingSettings` defaults | See §2 (discount) and §5 (entropy). |
| `GatherCompletions()` runs **before** `GatherRewards()` each step | `ULearningAgentsPPOTrainer::RunTraining` (C++) | The cause of C-004. Episode end must be a pure function of state (D-022). |

## 2. Discount and decision rate: how far ahead the agent can see
- The effective look-ahead of a discount γ is about `1 / (1 − γ)` decisions.
- With γ = 0.99 and **one decision per 60 Hz tick** (Spike A, D-019), that's about 100 decisions = **1.7 s**.
- A reward 20 s in the future is weighted by `0.99^1200 ≈ 6 × 10⁻⁶`, so the agent effectively never sees it. A "cleared all bots quickly" bonus would be invisible.
- Fix: **fewer decisions per second, and a higher discount.**

| Decisions/s | γ | Look-ahead | Weight of a reward 20 s away |
|---|---|---|---|
| 60 | 0.99 | 1.7 s | ≈ 0.000006 |
| 15 | 0.99 | 6.7 s | ≈ 0.05 |
| 15 | 0.995 | 13 s | ≈ 0.22 |
| 15 | 0.997 | 22 s | ≈ 0.41 |

- The README already planned 15–20 decisions per second for combat.
- Raising γ further makes value estimates noisier. 0.995 is the default; sweep 0.99–0.997.
- Supporting literature:
  - Per-step penalties speed up learning compared with rewarding only the goal, and the discount interacts with them (Sowerby et al., *Designing Rewards for Fast Learning*, 2022).
  - With γ < 1, a step penalty favours greedy behaviour (UAV coverage paper, 2023).

## 3. Reward shaping without creating loopholes
- **Potential-based shaping** (Ng, Harada & Russell, ICML 1999):
  - It has the form `F = γ·Φ(s') − Φ(s)`, where Φ scores how good a state is.
  - It is the only kind of added reward guaranteed not to change which policy is optimal.
  - Spike A's "+1 per metre closer" is essentially this.
  - Use it for any "get closer to something useful" hint.
- **One-off bonuses that can't be farmed are safe:**
  - first sighting of each enemy, once per life;
  - the clear bonus, once per episode (paid once: D-022).
- **Never reward a ratio the agent controls by not acting.**
  - Rewarding accuracy teaches the agent not to shoot.
  - So accuracy and headshot % stay metrics (README, CLAUDE.md).
- **Reward hacking is the default outcome, not an edge case.**
  - Log every term separately and watch which ones grow.
  - A term growing without the win rate growing is a red flag.
  - Gran Turismo Sophy (Wurman et al., *Nature* 2022) needed progress, collision, off-track and etiquette terms balanced together, because "the progress reward alone was not enough" to win races.
- **Scale and offset matter.** Constant reward scaling that matches the scale of the initial value estimates works as well as complex normalization schemes (*Revisiting Design Choices in PPO*, 2020; Engstrom et al., *Implementation Matters*, ICLR 2020).

## 4. What works for FPS agents specifically
- **Arnold** (Chaplot & Lample, AAAI 2017):
  - a Doom deathmatch agent;
  - uses reward shaping plus high-level game features during training;
  - has separate behaviours for navigation and fighting;
  - got the best kill/death ratio in the VizDoom competition.
- **Wu & Tian, ICLR 2017** (the "F1" Doom agent) used a curriculum over map types, opponent strength and reward design, and adapted the curriculum during training.
- **DeepMind's Capture the Flag agents** (Jaderberg et al., *Science* 2019):
  - the match result alone is too sparse to learn from;
  - each agent learns its own internal reward;
  - population-based training evolves those rewards and the hyperparameters;
  - opponents come from the population (self-play).
  - We can't copy that directly: self-play is forbidden (hard rule 3), and we keep the trainer files verbatim. What we take from it: dense intermediate rewards, randomized environments, and tuning reward weights by search, not by hand.

## 5. Optimization levers, in order of expected impact
1. **Discount and decision rate** (§2).
2. **Experience per update:**
   - `--batch-size` = the number of game processes, so no buffer is thrown away as stale (Spike A finding 3; Spike B);
   - `MaximumRecordedStepsPerIteration`.
3. **Exploration:**
   - `ActionEntropyWeight` is 0 by default.
   - Finding bots is a search problem, and too little exploration ends in camping. Sweep 0 / 0.001 / 0.01.
4. **Network size:**
   - The default is 1×128 + GRU 64. Combat observations are about 200+ numbers.
   - Start at 2×256 + memory 128 and check the speed in Spike B.
5. **Learning rate:**
   - Adam with LR 3e-4 is a safe default in large PPO studies (Andrychowicz et al., *What Matters in On-Policy RL*, 2020). Our defaults are 1e-4 policy / 1e-3 critic.
   - Sweep only after 1–4.
6. **PPO details that are already fine in our trainer:**
   - GAE (λ 0.95);
   - clipping (0.2);
   - separate policy and critic networks.
   - The "37 implementation details" checklist (Huang et al., ICLR 2022 blog track) is useful for reading training curves.

**How to tune:** cluster job-array sweeps (README → *Kinds of parallelism*), compared on fixed evaluation seeds, never on training curves alone. Training curves are noisy, and they were wrong in Spike A (C-004).

## 6. Curriculum: making training harder over time
- **Automatic curriculum learning** (Portelas et al., IJCAI 2020 survey) means choosing training tasks that match the agent's current skill, usually by tracking learning progress or success rate.
- **Prioritized Level Replay** (Jiang et al., ICML 2021):
  - replays procedurally generated levels with high TD error;
  - this produces an emergent easy-to-hard curriculum and better generalization.
  - It needs per-level TD errors from the trainer, which our game can't see (we don't modify the trainer).
- **What we do instead, game-side:**
  - Each arena tracks its recent clear rate.
  - Difficulty rises while the agent clears more than about 70% of episodes and eases off below about 40%. This is a success-rate controller, a simple form of the same idea.
  - Stages S0–S3 stay separate runs with promotion gates.
  - Procedural maps (our 50-map pool) give the variety that PLR exploits.
- **Forgetting:** when difficulty rises, keep sampling some easier episodes, about 20%, so early skills aren't lost.

## 7. Time-efficiency rewards (the clear bonus)
- No paper found compares "bonus for finishing fast" with "penalty per second" directly.
- They are closely related: a bonus that shrinks with time acts like extra discounting. It only works if the agent can see that far (§2).
- **Agreed design:** on clearing all bots, `0.5 + 1.5 × (fraction of the time limit left)`.
  - Max +2.0, about one bot's worth (+1 kill, about +1 damage).
  - At least +0.5 for any clear.
  - Rushing into a death (−1, plus losing the bonus) still costs more than it gains.

## Sources
- Ng, Harada, Russell (1999), *Policy invariance under reward transformations*: [AAAI-hosted summary](https://ojs.aaai.org/index.php/AAAI/article/view/9628/9487), [overview](https://www.emergentmind.com/topics/potential-based-shaping)
- Andrychowicz et al. (2020), *What Matters in On-Policy RL*: [arXiv 2006.05990](https://arxiv.org/pdf/2006.05990)
- Engstrom et al. (2020), *Implementation Matters in Deep Policy Gradients*: [arXiv 2005.12729](https://arxiv.org/pdf/2005.12729)
- *Revisiting Design Choices in PPO* (2020): [arXiv 2009.10897](https://arxiv.org/pdf/2009.10897)
- Huang et al. (2022), *The 37 Implementation Details of PPO*: [ICLR blog track](https://iclr-blog-track.github.io/2022/03/25/ppo-implementation-details)
- Chaplot & Lample (2017), *Arnold*: [AAAI](https://ojs.aaai.org/index.php/AAAI/article/view/10534)
- Jaderberg et al. (2019), *Human-level performance in 3D multiplayer games*: [DeepMind blog](https://deepmind.google/discover/blog/capture-the-flag-the-emergence-of-complex-cooperative-agents/), [paper PDF](https://people.eecs.berkeley.edu/~russell/classes/cs294/f21/papers/Jaderberg-etal-2019-Science-Capture-The-Flag.pdf)
- Portelas et al. (2020), *Automatic Curriculum Learning for Deep RL: A Short Survey*: [arXiv 2003.04664](https://ar5iv.labs.arxiv.org/html/2003.04664)
- Jiang et al. (2021), *Prioritized Level Replay*: [arXiv 2010.03934](https://arxiv.org/pdf/2010.03934v4.pdf)
- Sowerby et al. (2022), *Designing Rewards for Fast Learning*: [arXiv 2205.15400](https://arxiv.org/abs/2205.15400)
- UAV coverage path planning (2023), on discount vs. step penalty: [arXiv 2309.03157](https://arxiv.org/pdf/2309.03157)
- Wurman et al. (2022), *Outracing Champion Gran Turismo Drivers*: [UT Austin PDF](https://www.cs.utexas.edu/~pstone/Papers/bib2html-links/nature22.pdf), [Sony AI retrospective](https://ai.sony/blog/gran-turismo-sophy-five-years-on-from-nature-cover-to-open-frontier)
- Wu & Tian (2017), *Training Agent for First-Person Shooter Game with Actor-Critic Curriculum Learning*, ICLR 2017. Known from a search summary; link not checked.

import site
import sys
import json
import os

wandb_run = None

# Takes in the python executable path, the three wandb init strings, and optionally the current run ID
# Returns the ID of the run (either newly created or resumed)
def init(py_exec_path, project, group, name, id = None):

\tglobal wandb_run
\n\t# Fix the path of our interpreter so wandb doesn't run RLGym_PPO instead of Python
\t# Very strange fix for a very strange problem
\tsys.executable = py_exec_path
\n\ttry:
\t\tsite_packages_dir = os.path.join(os.path.join(os.path.dirname(py_exec_path), "Lib"), "site-packages")
\t\tsys.path.append(site_packages_dir)
\t\tsite.addsitedir(site_packages_dir)
\t\timport wandb
\texcept Exception as e:
\t\traise Exception(f"""
\t\t\tFAILED to import wandb! Make sure GigaLearnCPP isn't using the wrong Python installation.
\t\t\tThis installation's site packages: {site.getsitepackages()}
\t\t\tException: {repr(e)}""")
\n\t# GigaLearnCPP launches this Python helper from the C++ process. In Colab/Linux,
\t# W&B can have credentials present in the environment but still report the Python
\t# SDK as "not logged in" because the helper adjusts sys.executable/sys.path.
\t# Authenticate explicitly in this process before wandb.init().
\tapi_key = os.environ.get("WANDB_API_KEY", "").strip()
\tif not api_key:
\t\traise RuntimeError("WANDB_API_KEY is not available to metric_receiver.py")
\n\ttry:
\t\twandb.login(key=api_key, relogin=True, _silent=True)
\texcept Exception as e:
\t\traise RuntimeError(f"W&B authentication failed in metric_receiver.py: {e}")
\n\tprint("Calling wandb.init()...")
\tif not (id is None) and len(id) > 0:
\t\twandb_run = wandb.init(project = project, group = group, name = name, id = id, resume = "allow")
\telse:
\t\twandb_run = wandb.init(project = project, group = group, name = name)
\treturn wandb_run.id
\ndef add_metrics(metrics):
\tglobal wandb_run
\tif wandb_run is None:
\t\traise RuntimeError("W&B run is not initialized")
\twandb_run.log(metrics)

import site
import sys
import os

wandb_run = None


def init(py_exec_path, project, group, name, id=None):
    global wandb_run

    # Make sure the helper uses the Python environment that GigaLearnCPP requested.
    sys.executable = py_exec_path

    try:
        site_packages_dir = os.path.join(
            os.path.dirname(py_exec_path), "Lib", "site-packages"
        )
        if os.path.isdir(site_packages_dir):
            sys.path.append(site_packages_dir)
            site.addsitedir(site_packages_dir)
        import wandb
    except Exception as e:
        raise Exception(
            "FAILED to import wandb! Make sure GigaLearnCPP isn't using the wrong "
            "Python installation. "
            f"This installation's site packages: {site.getsitepackages()} "
            f"Exception: {repr(e)}"
        )

    api_key = os.environ.get("WANDB_API_KEY", "").strip()
    if not api_key:
        raise RuntimeError("WANDB_API_KEY is not available to metric_receiver.py")

    try:
        # Newer wandb SDKs removed the `_silent` kwarg from login(); fall back
        # gracefully instead of hard-failing auth.
        try:
            wandb.login(key=api_key, relogin=True, _silent=True)
        except TypeError:
            wandb.login(key=api_key, relogin=True)
    except Exception as e:
        raise RuntimeError(f"W&B authentication failed in metric_receiver.py: {e}")

    print("Calling wandb.init()...")
    if id:
        wandb_run = wandb.init(
            project=project,
            group=group,
            name=name,
            id=id,
            resume="allow",
        )
    else:
        wandb_run = wandb.init(project=project, group=group, name=name)

    return wandb_run.id


def add_metrics(metrics):
    global wandb_run
    if wandb_run is None:
        raise RuntimeError("W&B run is not initialized")
    wandb_run.log(metrics)

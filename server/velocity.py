from server.catalog import ANIMALS

__all__ = ["normalize_velocities"]


def normalize_velocities(organisms) -> None:
    sizes = tuple(
        max(int(ANIMALS[item.type][12]), int(ANIMALS[item.type][13])) * item.scale
        for item in organisms
    )

    product = 25 * min(sizes)
    for item, size in zip(organisms, sizes):
        item.velocity = product / size

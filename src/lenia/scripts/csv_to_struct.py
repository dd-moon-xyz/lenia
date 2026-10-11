# Convert the CSV File to CAnimalInfo Structs


def get_growth_function(growth_n: str) -> str:
    match growth_n:
        case "1":
            return "Lenia::GrowthFunction::QUAD4"
        case "2":
            return "Lenia::GrowthFunction::GAUSS"
        case "3":
            return "Lenia::GrowthFunction::STPZ"
        case _:
            raise ValueError("Invalid kernel n " + growth_n)


def get_kernel(kernel_n: str) -> str:
    match kernel_n:
        case "1":
            return "Lenia::KernelCore::QUAD4"
        case "2":
            return "Lenia::KernelCore::BUMP4"
        case "3":
            return "Lenia::KernelCore::STPZ"
        case "4":
            return "Lenia::KernelCore::LEAK"
        case _:
            raise ValueError("Invalid kernel n " + kernel_n)


def process_line(line: str, max_beta: int) -> str:
    tokens = line.split(",")
    _class, order, family, subfamily, species = tokens[:5]
    r = tokens[5]
    dt = 1 / int(tokens[6])
    beta = tokens[7].split(";")
    mu = tokens[8]
    sigma = tokens[9]
    kn = tokens[10]
    gn = tokens[11]
    w = tokens[12]
    h = tokens[13]
    rle = tokens[14]
    dx2 = 1 / (int(r) * int(r))
    sep = ",\n        "
    struct = f"""constexpr Lenia::CAnimalInfo {species.title().replace(" ", "").replace("&", "And").replace("-", "").replace("?", "")} {{
    "{species}",
    "{_class}",
    "{order}",
    "{subfamily}",
    "{family}",
    {r},
    {w},
    {h},
    10,
    {dt},
    {dx2},
    {mu},
    {sigma},
    {{
        {sep.join(beta)}
    }},
    {get_kernel(kn)},
    {get_growth_function(gn)},
    "{rle}"
}};"""
    return struct


def get_max_beta_length(csv: list[str]) -> int:
    return max(line.count(";") + 1 for line in csv)


def main() -> None:
    structs: list[str] = []
    with open("resources/animals_dim.csv", "r") as f:
        lines = f.readlines()
        max_beta = get_max_beta_length(lines)
        for line in lines:
            structs.append(process_line(line, max_beta))
    with open("test.cpp", "w") as f:
        f.write('#include "lenia/animal.hpp"\n\n' + "\n".join(structs))


if __name__ == "__main__":
    main()

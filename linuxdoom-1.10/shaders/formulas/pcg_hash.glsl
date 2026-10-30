// Cheap integer hash (PCG) -> a float in [0,1), for stochastic sampling.
uint pcgHash(uint x)
{
    uint state = x * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

# Optimize supplied chains before assembling them

SemantiFX will first accept an effect chain as an input and use CLAP to optimize
that chain's parameters toward a target prompt. It will not initially choose
effect types, instance counts, or ordering. Separating chain optimization from
chain assembly keeps the first search space bounded, lets us evaluate whether
prompt-driven parameter optimization is useful across effect types, and keeps
every suggestion inspectable. The reorderable EQ + distortion chain is the
first multi-effect slice of this broader model.

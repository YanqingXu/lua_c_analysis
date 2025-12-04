function factorial(n)
    if n <= 1 then
        return 1
    end
    return n * factorial(n - 1)
end
return factorial(5)


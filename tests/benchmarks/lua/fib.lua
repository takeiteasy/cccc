local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end

print(string.format("result: %d", fib(30)))
os.exit(42)

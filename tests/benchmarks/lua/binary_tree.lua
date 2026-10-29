local n = 100000
local s = 0x9E3779B97F4A7C15
local checksum, visit_count = 0, 0

local function mrand()
    s = s * 6364136223846793005 + 1442695040888963407
    return s >> 11
end

local function insert(node, k)
    if not node then return { key = k } end
    if k < node.key then
        node.left = insert(node.left, k)
    else
        node.right = insert(node.right, k)
    end
    return node
end

local function inorder(node)
    if not node then return end
    inorder(node.left)
    checksum = checksum + node.key
    visit_count = visit_count + 1
    inorder(node.right)
end

local root
for _ = 1, n do
    root = insert(root, mrand() & 0x7FFFFFFF)
end
inorder(root)
print(string.format("result: visits=%d checksum=%d", visit_count, checksum))
os.exit(42)

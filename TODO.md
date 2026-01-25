# TODO
- [ ] 支持使用/mock + @username 调用LLM模仿指定用户
- [ ] 支持qqbot
- [ ] 允许使用不同api key构建多个LLM实例避免速率限制


# Problems

## user_id搞混了
现在bot和user的"content"一栏中user_id都是一样的，例如：
```
{
    "content":"6545430341: 何意味是什么意思","role":"user"
},
{
    "content":"6545430341: .","role":"user"
},
{
    "content":"6545430341: “何意味”是什么意思","role":"assistant"
}
```

## config中的history_limit没读进去

送入请求的msg数目貌似仍然为10，config设置的是100

## Core 变更回退（asio 别名）

- **问题**：为修复编译测试临时在 `include/core/task_scheduler.hpp` 增加了 `namespace asio = boost::asio;`。

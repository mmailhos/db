# DB

Writing a sqlite clone from scratch in C.

![sqllite arch](https://cstack.github.io/db_tutorial/assets/images/arch2.gif)

## Format

```
id integer
username varchar(32)
email varchar(255)
```

Store rows in blocks of memory called pages.
Each pages correspond to a memory page.
Pages store as many rows as possible.
Rows are serialized into a compact representation with each page.
Pages are only allocated as needed. We keep an array of pointers to pages.

## References 

[cstack tutorial](https://cstack.github.io/)

const { LanguageClient } = require('vscode-languageclient/node');
const path = require('path');
const vscode = require('vscode');

let client;
let outputChannel;

async function startClient(context) {
    if (!outputChannel) {
        outputChannel = vscode.window.createOutputChannel('Ether');
    }
    outputChannel.appendLine('Ether extension is now active!');

    // The server is our ether executable
    // We assume it's in the build directory relative to the workspace
    const serverPath = vscode.workspace.getConfiguration('ether').get('compilerPath') || 'ether';
    outputChannel.appendLine(`Using compiler at: ${serverPath}`);

    const serverOptions = {
        command: serverPath,
        args: ['--lsp'],
        options: { shell: true }
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'ether' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.eth')
        },
        outputChannel: outputChannel
    };

    client = new LanguageClient(
        'etherLanguageServer',
        'Ether Language Server',
        serverOptions,
        clientOptions
    );

    outputChannel.appendLine('Starting Ether Language Server...');
    await client.start();
}

async function activate(context) {
    await startClient(context);

    context.subscriptions.push(vscode.commands.registerCommand('ether.restartServer', async () => {
        if (client) {
            outputChannel.appendLine('Restarting Ether Language Server...');
            await client.stop();
            client = undefined;
        }
        await startClient(context);
    }));
}

function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

module.exports = {
    activate,
    deactivate
};
